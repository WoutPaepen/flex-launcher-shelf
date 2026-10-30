#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL.h>
#include <SDL_image.h>
#include "render.h"
#include "hdr.h"

#define SHADOW_SIZE 1024
#define ENV_LEVELS 6      // roughness 0, 0.2 ... 1.0 live in mip levels 0..5
#define ENV_BASE_W 1024

typedef struct {
    GLuint prog;
    GLint u[64];
} Program;

// ---------- shader sources ----------

static const char *VS_STANDARD =
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec3 aNormal;\n"
    "layout(location=2) in vec2 aUv;\n"
    "uniform mat4 uModel, uView, uProj, uShadowMatrix;\n"
    "uniform mat3 uNormalMatrix;\n"
    "uniform float uShadowNormalBias;\n"
    "out vec3 vWorldPos; out vec3 vNormal; out vec2 vUv; out vec4 vShadow;\n"
    "void main() {\n"
    "  vec4 wp = uModel * vec4(aPos, 1.0);\n"
    "  vWorldPos = wp.xyz;\n"
    "  vNormal = normalize(uNormalMatrix * aNormal);\n"
    "  vUv = aUv;\n"
    "  vShadow = uShadowMatrix * vec4(wp.xyz + vNormal * uShadowNormalBias, 1.0);\n"
    "  gl_Position = uProj * uView * wp;\n"
    "}\n";

// Port of three.js r169 physical lighting: GGX + Smith correlated visibility,
// Schlick fresnel, IBL with multiscattering, optional clearcoat, PCF shadows,
// Khronos PBR Neutral tone mapping and sRGB output.
static const char *FS_STANDARD =
    "#define PI 3.141592653589793\n"
    "#define RECIPROCAL_PI 0.3183098861837907\n"
    "#define EPSILON 1e-6\n"
    "in vec3 vWorldPos; in vec3 vNormal; in vec2 vUv; in vec4 vShadow;\n"
    "uniform vec3 uColor; uniform float uRoughness, uMetalness;\n"
    "uniform sampler2D uMap, uRoughMap, uMetalMap, uNormalMap, uEnv;\n"
    "uniform sampler2DShadow uShadowMap;\n"
    "uniform int uHasMap, uHasRough, uHasMetal, uHasNormal, uReceiveShadow;\n"
    "uniform vec2 uMapRep, uRoughRep, uMetalRep, uNormalRep;\n"
    "uniform float uNormalScale, uClearcoat, uClearcoatRoughness;\n"
    "uniform vec3 uCamPos;\n"
    "uniform vec3 uKeyDir, uKeyColor;\n"
    "uniform float uShadowBias, uShadowRadius, uShadowSize;\n"
    "uniform vec3 uSpotPos[2], uSpotDir[2], uSpotColor[2];\n"
    "uniform float uSpotCone[2], uSpotPen[2], uSpotDist[2], uSpotDecay[2];\n"
    "uniform vec3 uPointPos, uPointColor; uniform float uPointDist, uPointDecay;\n"
    "uniform float uEnvIntensity, uEnvMaxLod, uExposure;\n"
    "out vec4 fragColor;\n"
    "float pow2(float x) { return x * x; }\n"
    "float pow4(float x) { float x2 = x * x; return x2 * x2; }\n"
    "vec3 F_Schlick(vec3 f0, float f90, float dotVH) {\n"
    "  float fresnel = exp2((-5.55473 * dotVH - 6.98316) * dotVH);\n"
    "  return f0 * (1.0 - fresnel) + (f90 * fresnel);\n"
    "}\n"
    "float V_GGX_SmithCorrelated(float alpha, float dotNL, float dotNV) {\n"
    "  float a2 = pow2(alpha);\n"
    "  float gv = dotNL * sqrt(a2 + (1.0 - a2) * pow2(dotNV));\n"
    "  float gl = dotNV * sqrt(a2 + (1.0 - a2) * pow2(dotNL));\n"
    "  return 0.5 / max(gv + gl, EPSILON);\n"
    "}\n"
    "float D_GGX(float alpha, float dotNH) {\n"
    "  float a2 = pow2(alpha);\n"
    "  float denom = pow2(dotNH) * (a2 - 1.0) + 1.0;\n"
    "  return RECIPROCAL_PI * a2 / pow2(denom);\n"
    "}\n"
    "vec3 BRDF_GGX(vec3 L, vec3 V, vec3 N, vec3 f0, float f90, float roughness) {\n"
    "  float alpha = pow2(roughness);\n"
    "  vec3 H = normalize(L + V);\n"
    "  float dotNL = clamp(dot(N, L), 0.0, 1.0);\n"
    "  float dotNV = clamp(dot(N, V), 0.0, 1.0);\n"
    "  float dotNH = clamp(dot(N, H), 0.0, 1.0);\n"
    "  float dotVH = clamp(dot(V, H), 0.0, 1.0);\n"
    "  return F_Schlick(f0, f90, dotVH) * (V_GGX_SmithCorrelated(alpha, dotNL, dotNV) * D_GGX(alpha, dotNH));\n"
    "}\n"
    "vec2 DFGApprox(vec3 N, vec3 V, float roughness) {\n"
    "  float dotNV = clamp(dot(N, V), 0.0, 1.0);\n"
    "  const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);\n"
    "  const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);\n"
    "  vec4 r = roughness * c0 + c1;\n"
    "  float a004 = min(r.x * r.x, exp2(-9.28 * dotNV)) * r.x + r.y;\n"
    "  return vec2(-1.04, 1.04) * a004 + r.zw;\n"
    "}\n"
    "float distanceAttenuation(float d, float cutoff, float decay) {\n"
    "  float falloff = 1.0 / max(pow(d, decay), 0.01);\n"
    "  if (cutoff > 0.0) falloff *= pow2(clamp(1.0 - pow4(d / cutoff), 0.0, 1.0));\n"
    "  return falloff;\n"
    "}\n"
    "vec3 envSample(vec3 dir, float roughness) {\n"
    "  vec3 d = vec3(-dir.x, dir.y, -dir.z);\n" // environmentRotation (0, PI, 0)
    "  vec2 uv = vec2(atan(d.z, d.x) * 0.15915494309189535 + 0.5, asin(clamp(d.y, -1.0, 1.0)) * RECIPROCAL_PI + 0.5);\n"
    "  return textureLod(uEnv, uv, clamp(roughness, 0.0, 1.0) * uEnvMaxLod).rgb;\n"
    "}\n"
    "vec3 envRadiance(vec3 V, vec3 N, float roughness) {\n"
    "  vec3 R = reflect(-V, N);\n"
    "  R = normalize(mix(R, N, roughness * roughness));\n"
    "  return envSample(R, roughness) * uEnvIntensity;\n"
    "}\n"
    "float shadowTap(vec2 uv, float z) { return texture(uShadowMap, vec3(uv, z)); }\n"
    "float getShadow() {\n"
    "  vec3 c = vShadow.xyz / vShadow.w;\n"
    "  c.z += uShadowBias;\n"
    "  if (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0 || c.z > 1.0) return 1.0;\n"
    "  float t = uShadowRadius / uShadowSize;\n"
    "  float dx0 = -t, dy0 = -t, dx1 = t, dy1 = t, dx2 = dx0 / 2.0, dy2 = dy0 / 2.0, dx3 = dx1 / 2.0, dy3 = dy1 / 2.0;\n"
    "  return (shadowTap(c.xy + vec2(dx0, dy0), c.z) + shadowTap(c.xy + vec2(0.0, dy0), c.z) + shadowTap(c.xy + vec2(dx1, dy0), c.z) +\n"
    "          shadowTap(c.xy + vec2(dx2, dy2), c.z) + shadowTap(c.xy + vec2(0.0, dy2), c.z) + shadowTap(c.xy + vec2(dx3, dy2), c.z) +\n"
    "          shadowTap(c.xy + vec2(dx0, 0.0), c.z) + shadowTap(c.xy + vec2(dx2, 0.0), c.z) + shadowTap(c.xy, c.z) +\n"
    "          shadowTap(c.xy + vec2(dx3, 0.0), c.z) + shadowTap(c.xy + vec2(dx1, 0.0), c.z) +\n"
    "          shadowTap(c.xy + vec2(dx2, dy3), c.z) + shadowTap(c.xy + vec2(0.0, dy3), c.z) + shadowTap(c.xy + vec2(dx3, dy3), c.z) +\n"
    "          shadowTap(c.xy + vec2(dx0, dy1), c.z) + shadowTap(c.xy + vec2(0.0, dy1), c.z) + shadowTap(c.xy + vec2(dx1, dy1), c.z)) * (1.0 / 17.0);\n"
    "}\n"
    "vec3 NeutralToneMapping(vec3 color) {\n"
    "  const float StartCompression = 0.8 - 0.04;\n"
    "  const float Desaturation = 0.15;\n"
    "  color *= uExposure;\n"
    "  float x = min(color.r, min(color.g, color.b));\n"
    "  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;\n"
    "  color -= offset;\n"
    "  float peak = max(color.r, max(color.g, color.b));\n"
    "  if (peak < StartCompression) return color;\n"
    "  float d = 1.0 - StartCompression;\n"
    "  float newPeak = 1.0 - d * d / (peak + d - StartCompression);\n"
    "  color *= newPeak / peak;\n"
    "  float g = 1.0 - 1.0 / (Desaturation * (peak - newPeak) + 1.0);\n"
    "  return mix(color, vec3(newPeak), g);\n"
    "}\n"
    "vec3 linearToSRGB(vec3 c) {\n"
    "  return mix(pow(c, vec3(0.41666)) * 1.055 - vec3(0.055), c * 12.92, vec3(lessThanEqual(c, vec3(0.0031308))));\n"
    "}\n"
    "struct Mat { vec3 diffuse; vec3 f0; float roughness; };\n"
    "void direct(vec3 L, vec3 color, vec3 N, vec3 V, vec3 ccN, float ccRough, Mat m, inout vec3 diff, inout vec3 spec, inout vec3 cc) {\n"
    "  float dotNL = clamp(dot(N, L), 0.0, 1.0);\n"
    "  vec3 irr = dotNL * color;\n"
    "  if (uClearcoat > 0.0) {\n"
    "    float dotNLcc = clamp(dot(ccN, L), 0.0, 1.0);\n"
    "    cc += dotNLcc * color * BRDF_GGX(L, V, ccN, vec3(0.04), 1.0, ccRough);\n"
    "  }\n"
    "  spec += irr * BRDF_GGX(L, V, N, m.f0, 1.0, m.roughness);\n"
    "  diff += irr * RECIPROCAL_PI * m.diffuse;\n"
    "}\n"
    "void main() {\n"
    "  vec3 N = normalize(vNormal);\n"
    "  if (!gl_FrontFacing) N = -N;\n"
    "  vec3 geoN = N;\n"
    "  vec3 V = normalize(uCamPos - vWorldPos);\n"
    "  vec3 dxy = max(abs(dFdx(geoN)), abs(dFdy(geoN)));\n"
    "  float geometryRoughness = max(max(dxy.x, dxy.y), dxy.z);\n"
    "  if (uHasNormal == 1) {\n"
    "    vec2 nuv = vUv * uNormalRep;\n"
    "    vec3 q0 = dFdx(vWorldPos), q1 = dFdy(vWorldPos);\n"
    "    vec2 st0 = dFdx(nuv), st1 = dFdy(nuv);\n"
    "    vec3 q1perp = cross(q1, N), q0perp = cross(N, q0);\n"
    "    vec3 T = q1perp * st0.x + q0perp * st1.x;\n"
    "    vec3 B = q1perp * st0.y + q0perp * st1.y;\n"
    "    float det = max(dot(T, T), dot(B, B));\n"
    "    float scale = det == 0.0 ? 0.0 : inversesqrt(det);\n"
    "    vec3 mapN = texture(uNormalMap, nuv).xyz * 2.0 - 1.0;\n"
    "    mapN.xy *= uNormalScale;\n"
    "    N = normalize(mat3(T * scale, B * scale, N) * mapN);\n"
    "  }\n"
    "  vec3 diffuseColor = uColor;\n"
    "  if (uHasMap == 1) diffuseColor *= texture(uMap, vUv * uMapRep).rgb;\n"
    "  float roughness = uRoughness;\n"
    "  if (uHasRough == 1) roughness *= texture(uRoughMap, vUv * uRoughRep).g;\n"
    "  float metalness = uMetalness;\n"
    "  if (uHasMetal == 1) metalness *= texture(uMetalMap, vUv * uMetalRep).b;\n"
    "  Mat m;\n"
    "  m.diffuse = diffuseColor * (1.0 - metalness);\n"
    "  m.f0 = mix(vec3(0.04), diffuseColor, metalness);\n"
    "  m.roughness = min(max(roughness, 0.0525) + geometryRoughness, 1.0);\n"
    "  float ccRough = min(max(uClearcoatRoughness, 0.0525) + geometryRoughness, 1.0);\n"
    "  vec3 ccN = geoN;\n"
    "  vec3 diff = vec3(0.0), spec = vec3(0.0), cc = vec3(0.0);\n"
    "  float shadow = uReceiveShadow == 1 ? getShadow() : 1.0;\n"
    "  direct(normalize(uKeyDir), uKeyColor * shadow, N, V, ccN, ccRough, m, diff, spec, cc);\n"
    "  for (int i = 0; i < 2; i++) {\n"
    "    vec3 lv = uSpotPos[i] - vWorldPos;\n"
    "    vec3 L = normalize(lv);\n"
    "    float att = smoothstep(uSpotCone[i], uSpotPen[i], dot(L, uSpotDir[i]));\n"
    "    if (att > 0.0) direct(L, uSpotColor[i] * att * distanceAttenuation(length(lv), uSpotDist[i], uSpotDecay[i]), N, V, ccN, ccRough, m, diff, spec, cc);\n"
    "  }\n"
    "  vec3 pv = uPointPos - vWorldPos;\n"
    "  direct(normalize(pv), uPointColor * distanceAttenuation(length(pv), uPointDist, uPointDecay), N, V, ccN, ccRough, m, diff, spec, cc);\n"
    // image based lighting with multiscattering
    "  vec3 radiance = envRadiance(V, N, m.roughness);\n"
    "  vec3 irradianceCos = envSample(N, 1.0) * uEnvIntensity;\n"
    "  vec2 fab = DFGApprox(N, V, m.roughness);\n"
    "  vec3 FssEss = m.f0 * fab.x + fab.y;\n"
    "  float Ess = fab.x + fab.y;\n"
    "  float Ems = 1.0 - Ess;\n"
    "  vec3 Favg = m.f0 + (1.0 - m.f0) * 0.047619;\n"
    "  vec3 Fms = FssEss * Favg / (1.0 - Ems * Favg);\n"
    "  vec3 multi = Fms * Ems;\n"
    "  vec3 total = FssEss + multi;\n"
    "  vec3 indDiffuse = m.diffuse * (1.0 - max(max(total.r, total.g), total.b)) * irradianceCos;\n"
    "  vec3 indSpec = radiance * FssEss + multi * irradianceCos;\n"
    "  vec3 outgoing = diff + spec + indDiffuse + indSpec;\n"
    "  if (uClearcoat > 0.0) {\n"
    "    vec2 cfab = DFGApprox(ccN, V, ccRough);\n"
    "    cc += envRadiance(V, ccN, ccRough) * (vec3(0.04) * cfab.x + cfab.y);\n"
    "    vec3 Fcc = F_Schlick(vec3(0.04), 1.0, clamp(dot(ccN, V), 0.0, 1.0));\n"
    "    outgoing = outgoing * (1.0 - uClearcoat * Fcc) + cc * uClearcoat;\n"
    "  }\n"
    "  fragColor = vec4(linearToSRGB(clamp(NeutralToneMapping(outgoing), 0.0, 1.0)), 1.0);\n"
    "}\n";

static const char *VS_SHADOW =
    "layout(location=0) in vec3 aPos;\n"
    "uniform mat4 uModel, uLight;\n"
    "void main() { gl_Position = uLight * uModel * vec4(aPos, 1.0); }\n";

static const char *FS_SHADOW =
    "out vec4 fragColor;\n"
    "void main() { fragColor = vec4(1.0); }\n";

// Unlit textured meshes: contact occlusion decals and the LED diffuser (not tone mapped)
static const char *VS_BASIC =
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=2) in vec2 aUv;\n"
    "uniform mat4 uModel, uView, uProj;\n"
    "out vec2 vUv;\n"
    "void main() { vUv = aUv; gl_Position = uProj * uView * uModel * vec4(aPos, 1.0); }\n";

static const char *FS_BASIC =
    "in vec2 vUv;\n"
    "uniform sampler2D uMap; uniform int uAlphaMap; uniform vec3 uColor; uniform float uOpacity;\n"
    "out vec4 fragColor;\n"
    "vec3 linearToSRGB(vec3 c) { return mix(pow(c, vec3(0.41666)) * 1.055 - vec3(0.055), c * 12.92, vec3(lessThanEqual(c, vec3(0.0031308)))); }\n"
    "void main() {\n"
    "  vec4 t = texture(uMap, vUv);\n"
    "  vec4 c = uAlphaMap == 1 ? vec4(uColor, uOpacity * t.g) : vec4(uColor * t.rgb, uOpacity * t.a);\n"
    "  fragColor = vec4(linearToSRGB(clamp(c.rgb, 0.0, 1.0)), c.a);\n"
    "}\n";

// Fullscreen background: the room or a photograph
static const char *VS_FULL =
    "out vec2 vNdc;\n"
    "void main() {\n"
    "  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;\n"
    "  vNdc = p;\n"
    "  gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";

static const char *FS_BACKGROUND =
    "#define RECIPROCAL_PI 0.3183098861837907\n"
    "in vec2 vNdc;\n"
    "uniform int uKind; uniform sampler2D uEnv, uPhotoA, uPhotoB;\n"
    "uniform mat3 uInvViewRot; uniform vec2 uTanHalf; uniform float uIntensity, uBlur, uEnvMaxLod, uMix, uExposure;\n"
    "out vec4 fragColor;\n"
    "vec3 NeutralToneMapping(vec3 color) {\n"
    "  const float StartCompression = 0.8 - 0.04; const float Desaturation = 0.15;\n"
    "  color *= uExposure;\n"
    "  float x = min(color.r, min(color.g, color.b));\n"
    "  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;\n"
    "  color -= offset;\n"
    "  float peak = max(color.r, max(color.g, color.b));\n"
    "  if (peak < StartCompression) return color;\n"
    "  float d = 1.0 - StartCompression;\n"
    "  float newPeak = 1.0 - d * d / (peak + d - StartCompression);\n"
    "  color *= newPeak / peak;\n"
    "  float g = 1.0 - 1.0 / (Desaturation * (peak - newPeak) + 1.0);\n"
    "  return mix(color, vec3(newPeak), g);\n"
    "}\n"
    "vec3 linearToSRGB(vec3 c) { return mix(pow(c, vec3(0.41666)) * 1.055 - vec3(0.055), c * 12.92, vec3(lessThanEqual(c, vec3(0.0031308)))); }\n"
    "void main() {\n"
    "  if (uKind == 1) {\n"
    "    vec3 dir = normalize(uInvViewRot * vec3(vNdc.x * uTanHalf.x, vNdc.y * uTanHalf.y, -1.0));\n"
    "    vec3 d = vec3(-dir.x, dir.y, -dir.z);\n"
    "    vec2 uv = vec2(atan(d.z, d.x) * 0.15915494309189535 + 0.5, asin(clamp(d.y, -1.0, 1.0)) * RECIPROCAL_PI + 0.5);\n"
    "    vec3 c = textureLod(uEnv, uv, uBlur * uEnvMaxLod).rgb * uIntensity;\n"
    "    fragColor = vec4(linearToSRGB(clamp(NeutralToneMapping(c), 0.0, 1.0)), 1.0);\n"
    "  } else {\n"
    "    vec2 uv = vNdc * 0.5 + 0.5;\n"
    "    vec3 c = mix(texture(uPhotoA, uv).rgb, texture(uPhotoB, uv).rgb, uMix) * uIntensity;\n"
    "    fragColor = vec4(linearToSRGB(clamp(c, 0.0, 1.0)), 1.0);\n"
    "  }\n"
    "}\n";

// GGX prefilter of the equirectangular room into one roughness per mip level
static const char *FS_PREFILTER =
    "#define PI 3.141592653589793\n"
    "in vec2 vNdc;\n"
    "uniform sampler2D uSrc; uniform float uRoughness, uSrcTexels; uniform int uSamples;\n"
    "out vec4 fragColor;\n"
    "vec2 dirToUv(vec3 d) { return vec2(atan(d.z, d.x) * 0.15915494309189535 + 0.5, asin(clamp(d.y, -1.0, 1.0)) / PI + 0.5); }\n"
    "vec3 uvToDir(vec2 uv) {\n"
    "  float phi = (uv.x - 0.5) * 2.0 * PI, theta = (uv.y - 0.5) * PI;\n"
    "  return vec3(cos(theta) * cos(phi), sin(theta), cos(theta) * sin(phi));\n"
    "}\n"
    "float radicalInverse(uint bits) {\n"
    "  bits = (bits << 16u) | (bits >> 16u);\n"
    "  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);\n"
    "  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);\n"
    "  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);\n"
    "  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);\n"
    "  return float(bits) * 2.3283064365386963e-10;\n"
    "}\n"
    "void main() {\n"
    "  vec2 uv = vNdc * 0.5 + 0.5;\n"
    "  vec3 N = uvToDir(uv);\n"
    "  if (uRoughness <= 0.0) { fragColor = vec4(textureLod(uSrc, uv, 1.0).rgb, 1.0); return; }\n"
    "  vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);\n"
    "  vec3 T = normalize(cross(up, N)), B = cross(N, T);\n"
    "  float a = uRoughness * uRoughness, a2 = a * a;\n"
    "  vec3 sum = vec3(0.0); float w = 0.0;\n"
    "  for (int i = 0; i < uSamples; i++) {\n"
    "    vec2 xi = vec2(float(i) / float(uSamples), radicalInverse(uint(i)));\n"
    "    float phi = 2.0 * PI * xi.x;\n"
    "    float cosT = sqrt((1.0 - xi.y) / (1.0 + (a2 - 1.0) * xi.y));\n"
    "    float sinT = sqrt(1.0 - cosT * cosT);\n"
    "    vec3 H = normalize(T * (cos(phi) * sinT) + B * (sin(phi) * sinT) + N * cosT);\n"
    "    vec3 L = normalize(2.0 * dot(N, H) * H - N);\n"
    "    float NdotL = dot(N, L);\n"
    "    if (NdotL > 0.0) {\n"
    "      float NdotH = max(dot(N, H), 0.0);\n"
    "      float d = NdotH * NdotH * (a2 - 1.0) + 1.0;\n"
    "      float D = a2 / (PI * d * d);\n"
    "      float pdf = D * 0.25 + 1e-4;\n"
    "      float saSample = 1.0 / (float(uSamples) * pdf);\n"
    "      float saTexel = 4.0 * PI / uSrcTexels;\n"
    "      float lod = max(0.5 * log2(saSample / saTexel) + 1.0, 0.0);\n"
    "      sum += textureLod(uSrc, dirToUv(L), lod).rgb * NdotL;\n"
    "      w += NdotL;\n"
    "    }\n"
    "  }\n"
    "  fragColor = vec4(sum / max(w, 1e-4), 1.0);\n"
    "}\n";

// 2D overlay: textures, rounded rectangles, the text scrim and the scene itself
static const char *VS_OVERLAY =
    "layout(location=0) in vec2 aPos;\n"
    "uniform vec4 uRect; uniform vec2 uScreen;\n"
    "out vec2 vLocal; out vec2 vUv;\n"
    "void main() {\n"
    "  vec2 p = uRect.xy + aPos * uRect.zw;\n"
    "  vLocal = aPos * uRect.zw;\n"
    "  vUv = aPos;\n"
    "  gl_Position = vec4(p.x / uScreen.x * 2.0 - 1.0, 1.0 - p.y / uScreen.y * 2.0, 0.0, 1.0);\n"
    "}\n";

static const char *FS_OVERLAY =
    "in vec2 vLocal; in vec2 vUv;\n"
    "uniform int uMode; uniform sampler2D uTex; uniform vec4 uColor, uBorderColor, uRect;\n"
    "uniform float uRadius, uBorder; uniform vec2 uScreen;\n"
    "out vec4 fragColor;\n"
    "float sdRound(vec2 p, vec2 b, float r) { vec2 q = abs(p) - b + r; return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }\n"
    "void main() {\n"
    "  if (uMode == 0) { float a = texture(uTex, vec2(vUv.x, 1.0 - vUv.y)).r; fragColor = vec4(uColor.rgb, uColor.a * a); return; }\n"
    "  if (uMode == 1) { vec4 t = texture(uTex, vec2(vUv.x, 1.0 - vUv.y)); fragColor = vec4(t.rgb * uColor.rgb, t.a * uColor.a); return; }\n"
    "  if (uMode == 3) { fragColor = vec4(texture(uTex, vec2(vUv.x, 1.0 - vUv.y)).rgb, uColor.a); return; }\n"
    "  if (uMode == 4) {\n"
    // the CSS scrim: a soft vignette plus darkening behind the top and bottom bars
    "    vec2 px = vec2(vUv.x, vUv.y);\n"
    "    vec2 q = (px - vec2(0.5, 0.55)) / vec2(1.2, 0.9);\n"
    "    float r = length(q);\n"
    "    float vig = clamp((r - 0.55) / (1.0 - 0.55), 0.0, 1.0) * 0.55;\n"
    "    float top = clamp(1.0 - px.y / 0.22, 0.0, 1.0) * 0.55;\n"
    "    float bottom = clamp(1.0 - (1.0 - px.y) / 0.20, 0.0, 1.0) * 0.6;\n"
    "    float a = 1.0 - (1.0 - vig) * (1.0 - top) * (1.0 - bottom);\n"
    "    fragColor = vec4(5.0 / 255.0, 5.0 / 255.0, 7.0 / 255.0, a * uColor.a);\n"
    "    return;\n"
    "  }\n"
    "  vec2 half_ = uRect.zw * 0.5;\n"
    "  float d = sdRound(vLocal - half_, half_, uRadius);\n"
    // a negative border draws a box shadow with that blur radius instead
    "  if (uBorder < 0.0) { float b = -uBorder; float a = 1.0 - smoothstep(-b, b, d + b); fragColor = vec4(uColor.rgb, uColor.a * a); return; }\n"
    "  float fill = clamp(0.5 - d, 0.0, 1.0);\n"
    "  float inner = clamp(0.5 - (d + uBorder), 0.0, 1.0);\n"
    "  vec4 c = uBorder > 0.0 ? mix(uBorderColor, uColor, inner) : uColor;\n"
    "  fragColor = vec4(c.rgb, c.a * fill);\n"
    "}\n";

enum {
    U_MODEL, U_VIEW, U_PROJ, U_NORMALMAT, U_SHADOWMAT, U_SHADOWNB,
    U_COLOR, U_ROUGH, U_METAL, U_MAP, U_ROUGHMAP, U_METALMAP, U_NORMALMAP, U_ENV, U_SHADOWMAP,
    U_HASMAP, U_HASROUGH, U_HASMETAL, U_HASNORMAL, U_RECEIVE,
    U_MAPREP, U_ROUGHREP, U_METALREP, U_NORMALREP, U_NSCALE, U_CC, U_CCROUGH, U_CAM,
    U_KEYDIR, U_KEYCOLOR, U_SBIAS, U_SRADIUS, U_SSIZE,
    U_SPOTPOS0, U_SPOTPOS1, U_SPOTDIR0, U_SPOTDIR1, U_SPOTCOL0, U_SPOTCOL1,
    U_SPOTCONE0, U_SPOTCONE1, U_SPOTPEN0, U_SPOTPEN1, U_SPOTDIST0, U_SPOTDIST1, U_SPOTDECAY0, U_SPOTDECAY1,
    U_POINTPOS, U_POINTCOL, U_POINTDIST, U_POINTDECAY, U_ENVINT, U_ENVLOD, U_EXPOSURE,
    U_COUNT
};
static const char *STANDARD_UNIFORMS[U_COUNT] = {
    "uModel", "uView", "uProj", "uNormalMatrix", "uShadowMatrix", "uShadowNormalBias",
    "uColor", "uRoughness", "uMetalness", "uMap", "uRoughMap", "uMetalMap", "uNormalMap", "uEnv", "uShadowMap",
    "uHasMap", "uHasRough", "uHasMetal", "uHasNormal", "uReceiveShadow",
    "uMapRep", "uRoughRep", "uMetalRep", "uNormalRep", "uNormalScale", "uClearcoat", "uClearcoatRoughness", "uCamPos",
    "uKeyDir", "uKeyColor", "uShadowBias", "uShadowRadius", "uShadowSize",
    "uSpotPos[0]", "uSpotPos[1]", "uSpotDir[0]", "uSpotDir[1]", "uSpotColor[0]", "uSpotColor[1]",
    "uSpotCone[0]", "uSpotCone[1]", "uSpotPen[0]", "uSpotPen[1]", "uSpotDist[0]", "uSpotDist[1]", "uSpotDecay[0]", "uSpotDecay[1]",
    "uPointPos", "uPointColor", "uPointDist", "uPointDecay", "uEnvIntensity", "uEnvMaxLod", "uExposure"
};

static struct {
    bool gles;
    char info[256];
    int win_w, win_h;
    int scene_w, scene_h;
    RenderQuality q;
    Program standard, shadow, basic, background, prefilter, overlay;
    GLuint shadow_fbo, shadow_tex;
    GLuint msaa_fbo, msaa_color, msaa_depth;
    GLuint scene_fbo, scene_tex, scene_depth;
    GLuint env_tex;
    GLuint white_tex;
    GLuint empty_vao;
    GLuint quad_vao, quad_vbo;
    mat4 shadow_matrix, light_vp;
    const Camera *cam;
    const Lights *lights;
    float aniso;
} R;

// ---------- programs ----------

static GLuint compile(GLenum type, const char *src)
{
    const char *header = R.gles
        ? "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2DShadow;\n"
        : "#version 330 core\n";
    const char *parts[2] = { header, src };
    GLuint s = glCreateShader(type);
    glShaderSource(s, 2, parts, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Shader compile failed:\n%s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static bool program(Program *p, const char *vs, const char *fs, const char **uniforms, int n)
{
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f)
        return false;
    p->prog = glCreateProgram();
    glAttachShader(p->prog, v);
    glAttachShader(p->prog, f);
    glLinkProgram(p->prog);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p->prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p->prog, sizeof(log), NULL, log);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Program link failed:\n%s", log);
        return false;
    }
    for (int i = 0; i < n; i++)
        p->u[i] = glGetUniformLocation(p->prog, uniforms[i]);
    return true;
}

static GLint uloc(const Program *p, const char *name)
{
    return glGetUniformLocation(p->prog, name);
}

// ---------- geometry ----------

typedef struct {
    float *v;
    int n, cap;
} VBuf;

static void vpush(VBuf *b, float px, float py, float pz, float nx, float ny, float nz, float u, float v)
{
    if (b->n + 8 > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 1024;
        b->v = realloc(b->v, sizeof(float) * (size_t) b->cap);
    }
    float *d = b->v + b->n;
    d[0] = px; d[1] = py; d[2] = pz; d[3] = nx; d[4] = ny; d[5] = nz; d[6] = u; d[7] = v;
    b->n += 8;
}

static Mesh mesh_upload(VBuf *b)
{
    Mesh m;
    m.count = b->n / 8;
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr) (sizeof(float) * (size_t) b->n), b->v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*) 0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*) 12);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*) 24);
    glBindVertexArray(0);
    free(b->v);
    return m;
}

// three.js BoxGeometry.buildPlane, emitted as non-indexed triangles
static void build_plane(VBuf *b, int u, int v, int w, float udir, float vdir, float width, float height, float depth, int gx, int gy)
{
    float seg_w = width / (float) gx, seg_h = height / (float) gy;
    float half_w = width / 2.f, half_h = height / 2.f, half_d = depth / 2.f;
    int gx1 = gx + 1;
    float *pos = malloc(sizeof(float) * 3 * (size_t) ((gx + 1) * (gy + 1)));
    float *uvs = malloc(sizeof(float) * 2 * (size_t) ((gx + 1) * (gy + 1)));
    float nrm[3] = { 0, 0, 0 };
    nrm[w] = depth > 0.f ? 1.f : -1.f;
    for (int iy = 0; iy <= gy; iy++) {
        float y = (float) iy * seg_h - half_h;
        for (int ix = 0; ix <= gx; ix++) {
            float x = (float) ix * seg_w - half_w;
            float *p = pos + (iy * gx1 + ix) * 3;
            p[u] = x * udir;
            p[v] = y * vdir;
            p[w] = half_d;
            uvs[(iy * gx1 + ix) * 2] = (float) ix / (float) gx;
            uvs[(iy * gx1 + ix) * 2 + 1] = 1.f - (float) iy / (float) gy;
        }
    }
    for (int iy = 0; iy < gy; iy++) {
        for (int ix = 0; ix < gx; ix++) {
            int a = ix + gx1 * iy, bb = ix + gx1 * (iy + 1), c = (ix + 1) + gx1 * (iy + 1), d = (ix + 1) + gx1 * iy;
            int tri[6] = { a, bb, d, bb, c, d };
            for (int k = 0; k < 6; k++) {
                float *p = pos + tri[k] * 3, *t = uvs + tri[k] * 2;
                vpush(b, p[0], p[1], p[2], nrm[0], nrm[1], nrm[2], t[0], t[1]);
            }
        }
    }
    free(pos);
    free(uvs);
}

static void build_box(VBuf *b, float w, float h, float d, int ws, int hs, int ds)
{
    build_plane(b, 2, 1, 0, -1, -1, d, h, w, ds, hs);  // px
    build_plane(b, 2, 1, 0, 1, -1, d, h, -w, ds, hs);  // nx
    build_plane(b, 0, 2, 1, 1, 1, w, d, h, ws, ds);    // py
    build_plane(b, 0, 2, 1, 1, -1, w, d, -h, ws, ds);  // ny
    build_plane(b, 0, 1, 2, 1, -1, w, h, d, ws, hs);   // pz
    build_plane(b, 0, 1, 2, -1, -1, w, h, -d, ws, hs); // nz
}

Mesh mesh_box(float w, float h, float d)
{
    VBuf b = { 0 };
    build_box(&b, w, h, d, 1, 1, 1);
    return mesh_upload(&b);
}

Mesh mesh_plane(float w, float h)
{
    VBuf b = { 0 };
    build_plane(&b, 0, 1, 2, 1, -1, w, h, 1, 1, 1);
    return mesh_upload(&b);
}

static float fsign(float x) { return x > 0.f ? 1.f : (x < 0.f ? -1.f : 0.f); }

// three.js RoundedBoxGeometry getUv. The side of the face comes from the unit-box position:
// on the flat middle strip the normal component is exactly zero, where three.js relies on
// double-precision noise to pick a side.
static float rb_uv(vec3 face, vec3 n, vec3 p, int uv_axis, int proj_axis, float radius, float side)
{
    float tot_arc = 2.f * VM_PI * radius / 4.f;
    float center = fmaxf(side - 2.f * radius, 0.f);
    float half_arc = VM_PI / 4.f;
    float t[3] = { n.x, n.y, n.z };
    t[proj_axis] = 0.f;
    vec3 tn = v3_norm(v3(t[0], t[1], t[2]));
    float arc_uv = 0.5f * tot_arc / (tot_arc + center);
    float angle = acosf(clampf(v3_dot(tn, face), -1.f, 1.f));
    float arc_angle = 1.f - angle / half_arc;
    float axis_val = uv_axis == 0 ? p.x : (uv_axis == 1 ? p.y : p.z);
    if (axis_val > 0.f)
        return arc_angle * arc_uv;
    float len_uv = center / (tot_arc + center);
    return len_uv + arc_uv + arc_uv * (1.f - arc_angle);
}

Mesh mesh_rounded_box(float width, float height, float depth, int segments, float radius)
{
    segments = segments * 2 + 1;
    radius = fminf(fminf(width / 2.f, height / 2.f), fminf(depth / 2.f, radius));
    VBuf b = { 0 };
    build_box(&b, 1, 1, 1, segments, segments, segments);
    vec3 box = v3(width / 2.f - radius, height / 2.f - radius, depth / 2.f - radius);
    int verts = b.n / 8, per_face = verts / 6;
    float half_seg = 0.5f / (float) segments;
    for (int i = 0; i < verts; i++) {
        float *d = b.v + i * 8;
        vec3 p = v3(d[0], d[1], d[2]);
        vec3 n = v3_norm(v3(p.x - fsign(p.x) * half_seg, p.y - fsign(p.y) * half_seg, p.z - fsign(p.z) * half_seg));
        d[0] = box.x * fsign(p.x) + n.x * radius;
        d[1] = box.y * fsign(p.y) + n.y * radius;
        d[2] = box.z * fsign(p.z) + n.z * radius;
        d[3] = n.x; d[4] = n.y; d[5] = n.z;
        // the second coordinate measures its arc against the up (or forward) direction, as three.js does
        switch (i / per_face) {
            case 0: // right
                d[6] = rb_uv(v3(1, 0, 0), n, p, 2, 1, radius, depth);
                d[7] = 1.f - rb_uv(v3(0, 1, 0), n, p, 1, 2, radius, height);
                break;
            case 1: // left
                d[6] = 1.f - rb_uv(v3(-1, 0, 0), n, p, 2, 1, radius, depth);
                d[7] = 1.f - rb_uv(v3(0, 1, 0), n, p, 1, 2, radius, height);
                break;
            case 2: // top
                d[6] = 1.f - rb_uv(v3(0, 1, 0), n, p, 0, 2, radius, width);
                d[7] = 1.f - rb_uv(v3(0, 0, 1), n, p, 2, 0, radius, depth);
                break;
            case 3: // bottom
                d[6] = 1.f - rb_uv(v3(0, -1, 0), n, p, 0, 2, radius, width);
                d[7] = 1.f - rb_uv(v3(0, 0, 1), n, p, 2, 0, radius, depth);
                break;
            case 4: // front
                d[6] = 1.f - rb_uv(v3(0, 0, 1), n, p, 0, 1, radius, width);
                d[7] = 1.f - rb_uv(v3(0, 1, 0), n, p, 1, 0, radius, height);
                break;
            default: // back
                d[6] = rb_uv(v3(0, 0, -1), n, p, 0, 1, radius, width);
                d[7] = 1.f - rb_uv(v3(0, 1, 0), n, p, 1, 0, radius, height);
                break;
        }
    }
    return mesh_upload(&b);
}

void mesh_free(Mesh *m)
{
    if (m->vbo)
        glDeleteBuffers(1, &m->vbo);
    if (m->vao)
        glDeleteVertexArrays(1, &m->vao);
    m->vbo = m->vao = 0;
}

// ---------- textures ----------

static void tex_params(bool repeat, bool mipmap)
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    if (mipmap) {
        glGenerateMipmap(GL_TEXTURE_2D);
        if (R.aniso > 1.f)
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, R.aniso);
    }
}

// Images are flipped on upload, like three.js (flipY), so v = 0 is the bottom row
static Tex tex_from_rgba(const uint8_t *px, int w, int h, int pitch, bool srgb, bool repeat, bool mipmap)
{
    Tex t = { 0, w, h };
    uint8_t *flipped = malloc((size_t) w * (size_t) h * 4);
    for (int y = 0; y < h; y++)
        memcpy(flipped + (size_t) y * (size_t) w * 4, px + (size_t) (h - 1 - y) * (size_t) pitch, (size_t) w * 4);
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, flipped);
    free(flipped);
    tex_params(repeat, mipmap);
    return t;
}

Tex tex_from_canvas(const Canvas *c, bool srgb, bool repeat, bool mipmap)
{
    return tex_from_rgba(c->px, c->w, c->h, c->w * 4, srgb, repeat, mipmap);
}

Tex tex_from_file(const char *path, bool srgb, bool repeat)
{
    Tex t = { 0, 0, 0 };
    SDL_Surface *s = IMG_Load(path);
    if (s == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load %s: %s", path, IMG_GetError());
        return t;
    }
    SDL_Surface *conv = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(s);
    if (conv == NULL)
        return t;
    SDL_LockSurface(conv);
    t = tex_from_rgba(conv->pixels, conv->w, conv->h, conv->pitch, srgb, repeat, true);
    SDL_UnlockSurface(conv);
    SDL_FreeSurface(conv);
    return t;
}

void tex_free(Tex *t)
{
    if (t->id)
        glDeleteTextures(1, &t->id);
    t->id = 0;
}

// ---------- environment ----------

static void full_triangle(void)
{
    glBindVertexArray(R.empty_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

bool render_load_environment(const char *hdr_path, float saturation, float red, float blue)
{
    HdrImage img;
    if (!hdr_load(hdr_path, &img)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load environment %s", hdr_path);
        return false;
    }
    // pull the photograph toward neutral graphite so the one warm light stays the lamp
    size_t n = (size_t) img.w * (size_t) img.h;
    for (size_t i = 0; i < n; i++) {
        float *p = img.rgb + i * 3;
        float l = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
        p[0] = (l + (p[0] - l) * saturation) * red;
        p[1] = l + (p[1] - l) * saturation;
        p[2] = (l + (p[2] - l) * saturation) * blue;
    }
    // upload the source bottom row first so v = 1 is the top of the photograph
    float *flipped = malloc(n * 3 * sizeof(float));
    for (int y = 0; y < img.h; y++)
        memcpy(flipped + (size_t) y * (size_t) img.w * 3, img.rgb + (size_t) (img.h - 1 - y) * (size_t) img.w * 3, (size_t) img.w * 3 * sizeof(float));
    GLuint src;
    glGenTextures(1, &src);
    glBindTexture(GL_TEXTURE_2D, src);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, img.w, img.h, 0, GL_RGB, GL_FLOAT, flipped);
    free(flipped);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    float src_texels = (float) img.w * (float) img.h;
    hdr_free(&img);

    if (R.env_tex)
        glDeleteTextures(1, &R.env_tex);
    glGenTextures(1, &R.env_tex);
    glBindTexture(GL_TEXTURE_2D, R.env_tex);
    for (int lvl = 0; lvl < ENV_LEVELS; lvl++) {
        int w = ENV_BASE_W >> lvl, h = (ENV_BASE_W / 2) >> lvl;
        glTexImage2D(GL_TEXTURE_2D, lvl, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, ENV_LEVELS - 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glUseProgram(R.prefilter.prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src);
    glUniform1i(uloc(&R.prefilter, "uSrc"), 0);
    glUniform1f(uloc(&R.prefilter, "uSrcTexels"), src_texels);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    for (int lvl = 0; lvl < ENV_LEVELS; lvl++) {
        int w = ENV_BASE_W >> lvl, h = (ENV_BASE_W / 2) >> lvl;
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, R.env_tex, lvl);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Environment prefilter target incomplete");
            break;
        }
        glViewport(0, 0, w, h);
        glUniform1f(uloc(&R.prefilter, "uRoughness"), (float) lvl / (float) (ENV_LEVELS - 1));
        glUniform1i(uloc(&R.prefilter, "uSamples"), lvl == 0 ? 1 : (lvl < 3 ? 128 : 256));
        full_triangle();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &src);
    return true;
}

// ---------- targets ----------

static void free_targets(void)
{
    if (R.msaa_fbo) glDeleteFramebuffers(1, &R.msaa_fbo);
    if (R.msaa_color) glDeleteRenderbuffers(1, &R.msaa_color);
    if (R.msaa_depth) glDeleteRenderbuffers(1, &R.msaa_depth);
    if (R.scene_fbo) glDeleteFramebuffers(1, &R.scene_fbo);
    if (R.scene_tex) glDeleteTextures(1, &R.scene_tex);
    if (R.scene_depth) glDeleteRenderbuffers(1, &R.scene_depth);
    R.msaa_fbo = R.msaa_color = R.msaa_depth = R.scene_fbo = R.scene_tex = R.scene_depth = 0;
}

static void make_targets(void)
{
    free_targets();
    int w = R.scene_w, h = R.scene_h;
    glGenTextures(1, &R.scene_tex);
    glBindTexture(GL_TEXTURE_2D, R.scene_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &R.scene_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, R.scene_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, R.scene_tex, 0);
    if (R.q.msaa <= 0) {
        glGenRenderbuffers(1, &R.scene_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, R.scene_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, R.scene_depth);
    }
    else {
        GLint max_samples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
        int samples = R.q.msaa < max_samples ? R.q.msaa : max_samples;
        glGenFramebuffers(1, &R.msaa_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, R.msaa_fbo);
        glGenRenderbuffers(1, &R.msaa_color);
        glBindRenderbuffer(GL_RENDERBUFFER, R.msaa_color);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, R.msaa_color);
        glGenRenderbuffers(1, &R.msaa_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, R.msaa_depth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, R.msaa_depth);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Scene framebuffer incomplete");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void render_resize(int win_w, int win_h, RenderQuality q)
{
    R.win_w = win_w;
    R.win_h = win_h;
    R.q = q;
    // the scene may draw below window resolution; the overlay always draws at full resolution
    float scale = q.scale > 0.f ? q.scale : 1.f;
    R.scene_w = (int) lroundf((float) win_w * scale);
    R.scene_h = (int) lroundf((float) win_h * scale);
    if (R.scene_w < 16) R.scene_w = 16;
    if (R.scene_h < 16) R.scene_h = 16;
    make_targets();
}

float render_scale(void)
{
    return (float) R.scene_w / (float) R.win_w;
}

const char *render_info(void)
{
    return R.info;
}

bool render_init(bool gles, int win_w, int win_h, RenderQuality q)
{
    memset(&R, 0, sizeof(R));
    R.gles = gles;
    snprintf(R.info, sizeof(R.info), "%s | %s", (const char*) glGetString(GL_VERSION), (const char*) glGetString(GL_RENDERER));
    if (gl_has_extension("GL_EXT_texture_filter_anisotropic") || gl_has_extension("GL_ARB_texture_filter_anisotropic")) {
        float max_aniso = 1.f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &max_aniso);
        R.aniso = fminf(max_aniso, 16.f);
    }
    if (!program(&R.standard, VS_STANDARD, FS_STANDARD, STANDARD_UNIFORMS, U_COUNT) ||
        !program(&R.shadow, VS_SHADOW, FS_SHADOW, NULL, 0) ||
        !program(&R.basic, VS_BASIC, FS_BASIC, NULL, 0) ||
        !program(&R.background, VS_FULL, FS_BACKGROUND, NULL, 0) ||
        !program(&R.prefilter, VS_FULL, FS_PREFILTER, NULL, 0) ||
        !program(&R.overlay, VS_OVERLAY, FS_OVERLAY, NULL, 0))
        return false;

    glGenVertexArrays(1, &R.empty_vao);
    float quad[12] = { 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 1 };
    glGenVertexArrays(1, &R.quad_vao);
    glBindVertexArray(R.quad_vao);
    glGenBuffers(1, &R.quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, R.quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void*) 0);
    glBindVertexArray(0);

    uint8_t white[4] = { 255, 255, 255, 255 };
    glGenTextures(1, &R.white_tex);
    glBindTexture(GL_TEXTURE_2D, R.white_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    // shadow map for the key light
    glGenTextures(1, &R.shadow_tex);
    glBindTexture(GL_TEXTURE_2D, R.shadow_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_SIZE, SHADOW_SIZE, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &R.shadow_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, R.shadow_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, R.shadow_tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    render_resize(win_w, win_h, q);
    return true;
}

void render_quit(void)
{
    free_targets();
}

// ---------- shadow pass ----------

void render_shadow_begin(const Lights *l)
{
    // orthographic shadow camera around the shelf (left -9, right 9, top 6, bottom -2)
    mat4 view = m4_look_at(l->key_pos, l->key_target, v3(0, 1, 0));
    mat4 proj = m4_ortho(-9.f, 9.f, -2.f, 6.f, 2.f, 40.f);
    R.light_vp = m4_mul(proj, view);
    mat4 bias = m4_mul(m4_translate(0.5f, 0.5f, 0.5f), m4_scale(0.5f, 0.5f, 0.5f));
    R.shadow_matrix = m4_mul(bias, R.light_vp);
    glBindFramebuffer(GL_FRAMEBUFFER, R.shadow_fbo);
    glViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    // like three.js, front-side materials cast from their back faces
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(R.shadow.prog);
    glUniformMatrix4fv(uloc(&R.shadow, "uLight"), 1, GL_FALSE, R.light_vp.m);
}

void render_shadow_mesh(const Mesh *m, mat4 model)
{
    glUniformMatrix4fv(uloc(&R.shadow, "uModel"), 1, GL_FALSE, model.m);
    glBindVertexArray(m->vao);
    glDrawArrays(GL_TRIANGLES, 0, m->count);
}

void render_shadow_end(void)
{
    glCullFace(GL_BACK);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------- scene pass ----------

static void bind_tex(int unit, GLuint id)
{
    glActiveTexture(GL_TEXTURE0 + (GLenum) unit);
    glBindTexture(GL_TEXTURE_2D, id ? id : R.white_tex);
}

void render_scene_begin(const Camera *cam, const Lights *l, const Background *bg)
{
    R.cam = cam;
    R.lights = l;
    glBindFramebuffer(GL_FRAMEBUFFER, R.msaa_fbo ? R.msaa_fbo : R.scene_fbo);
    glViewport(0, 0, R.scene_w, R.scene_h);
    glClearColor(bg->clear[0], bg->clear[1], bg->clear[2], 1.f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (bg->kind != BG_CLEAR) {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glUseProgram(R.background.prog);
        glUniform1i(uloc(&R.background, "uKind"), bg->kind == BG_ENV ? 1 : 2);
        // camera rotation only, as columns of the inverse view
        float inv[9] = {
            cam->view.m[0], cam->view.m[4], cam->view.m[8],
            cam->view.m[1], cam->view.m[5], cam->view.m[9],
            cam->view.m[2], cam->view.m[6], cam->view.m[10]
        };
        glUniformMatrix3fv(uloc(&R.background, "uInvViewRot"), 1, GL_FALSE, inv);
        glUniform2f(uloc(&R.background, "uTanHalf"), 1.f / cam->proj.m[0], 1.f / cam->proj.m[5]);
        glUniform1f(uloc(&R.background, "uIntensity"), bg->intensity);
        glUniform1f(uloc(&R.background, "uBlur"), bg->blurriness);
        glUniform1f(uloc(&R.background, "uEnvMaxLod"), (float) (ENV_LEVELS - 1));
        glUniform1f(uloc(&R.background, "uMix"), bg->photo_mix);
        glUniform1f(uloc(&R.background, "uExposure"), l->exposure);
        bind_tex(0, R.env_tex);
        glUniform1i(uloc(&R.background, "uEnv"), 0);
        bind_tex(1, bg->photo_a);
        glUniform1i(uloc(&R.background, "uPhotoA"), 1);
        bind_tex(2, bg->photo_b ? bg->photo_b : bg->photo_a);
        glUniform1i(uloc(&R.background, "uPhotoB"), 2);
        full_triangle();
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);

    const Program *p = &R.standard;
    glUseProgram(p->prog);
    glUniformMatrix4fv(p->u[U_VIEW], 1, GL_FALSE, cam->view.m);
    glUniformMatrix4fv(p->u[U_PROJ], 1, GL_FALSE, cam->proj.m);
    glUniformMatrix4fv(p->u[U_SHADOWMAT], 1, GL_FALSE, R.shadow_matrix.m);
    glUniform1f(p->u[U_SHADOWNB], 0.02f);
    glUniform3f(p->u[U_CAM], cam->eye.x, cam->eye.y, cam->eye.z);
    vec3 kd = v3_norm(v3_sub(l->key_pos, l->key_target));
    glUniform3f(p->u[U_KEYDIR], kd.x, kd.y, kd.z);
    glUniform3f(p->u[U_KEYCOLOR], l->key_color.x, l->key_color.y, l->key_color.z);
    glUniform1f(p->u[U_SBIAS], -0.0005f);
    glUniform1f(p->u[U_SRADIUS], 2.f);
    glUniform1f(p->u[U_SSIZE], (float) SHADOW_SIZE);
    for (int i = 0; i < 2; i++) {
        const SpotLight *s = &l->spot[i];
        vec3 dir = v3_norm(v3_sub(s->pos, s->target));
        glUniform3f(p->u[U_SPOTPOS0 + i], s->pos.x, s->pos.y, s->pos.z);
        glUniform3f(p->u[U_SPOTDIR0 + i], dir.x, dir.y, dir.z);
        glUniform3f(p->u[U_SPOTCOL0 + i], s->color.x, s->color.y, s->color.z);
        glUniform1f(p->u[U_SPOTCONE0 + i], cosf(s->angle));
        glUniform1f(p->u[U_SPOTPEN0 + i], cosf(s->angle * (1.f - s->penumbra)));
        glUniform1f(p->u[U_SPOTDIST0 + i], s->distance);
        glUniform1f(p->u[U_SPOTDECAY0 + i], s->decay);
    }
    glUniform3f(p->u[U_POINTPOS], l->point.pos.x, l->point.pos.y, l->point.pos.z);
    glUniform3f(p->u[U_POINTCOL], l->point.color.x, l->point.color.y, l->point.color.z);
    glUniform1f(p->u[U_POINTDIST], l->point.distance);
    glUniform1f(p->u[U_POINTDECAY], l->point.decay);
    glUniform1f(p->u[U_ENVINT], l->env_intensity);
    glUniform1f(p->u[U_ENVLOD], (float) (ENV_LEVELS - 1));
    glUniform1f(p->u[U_EXPOSURE], l->exposure);
    glUniform1i(p->u[U_MAP], 0);
    glUniform1i(p->u[U_ROUGHMAP], 1);
    glUniform1i(p->u[U_METALMAP], 2);
    glUniform1i(p->u[U_NORMALMAP], 3);
    glUniform1i(p->u[U_ENV], 4);
    glUniform1i(p->u[U_SHADOWMAP], 5);
    bind_tex(4, R.env_tex);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, R.shadow_tex);
}

void render_mesh(const Mesh *m, const Material *mat, mat4 model)
{
    const Program *p = &R.standard;
    float nm[9];
    m4_normal_matrix(model, nm);
    glUniformMatrix4fv(p->u[U_MODEL], 1, GL_FALSE, model.m);
    glUniformMatrix3fv(p->u[U_NORMALMAT], 1, GL_FALSE, nm);
    glUniform3f(p->u[U_COLOR], mat->color[0], mat->color[1], mat->color[2]);
    glUniform1f(p->u[U_ROUGH], mat->roughness);
    glUniform1f(p->u[U_METAL], mat->metalness);
    glUniform1i(p->u[U_HASMAP], mat->map != 0);
    glUniform1i(p->u[U_HASROUGH], mat->rough_map != 0);
    glUniform1i(p->u[U_HASMETAL], mat->metal_map != 0);
    glUniform1i(p->u[U_HASNORMAL], mat->normal_map != 0);
    glUniform1i(p->u[U_RECEIVE], mat->receive_shadow);
    glUniform2f(p->u[U_MAPREP], mat->map_repeat[0], mat->map_repeat[1]);
    glUniform2f(p->u[U_ROUGHREP], mat->rough_repeat[0], mat->rough_repeat[1]);
    glUniform2f(p->u[U_METALREP], mat->metal_repeat[0], mat->metal_repeat[1]);
    glUniform2f(p->u[U_NORMALREP], mat->normal_repeat[0], mat->normal_repeat[1]);
    glUniform1f(p->u[U_NSCALE], mat->normal_scale);
    glUniform1f(p->u[U_CC], mat->clearcoat);
    glUniform1f(p->u[U_CCROUGH], mat->clearcoat_roughness);
    bind_tex(0, mat->map);
    bind_tex(1, mat->rough_map);
    bind_tex(2, mat->metal_map);
    bind_tex(3, mat->normal_map);
    glBindVertexArray(m->vao);
    glDrawArrays(GL_TRIANGLES, 0, m->count);
}

static void basic_draw(const Mesh *m, GLuint tex, bool alpha_map, const float color[3], float opacity, mat4 model)
{
    const Program *p = &R.basic;
    glUseProgram(p->prog);
    glUniformMatrix4fv(uloc(p, "uModel"), 1, GL_FALSE, model.m);
    glUniformMatrix4fv(uloc(p, "uView"), 1, GL_FALSE, R.cam->view.m);
    glUniformMatrix4fv(uloc(p, "uProj"), 1, GL_FALSE, R.cam->proj.m);
    glUniform1i(uloc(p, "uAlphaMap"), alpha_map);
    glUniform3f(uloc(p, "uColor"), color[0], color[1], color[2]);
    glUniform1f(uloc(p, "uOpacity"), opacity);
    bind_tex(0, tex);
    glUniform1i(uloc(p, "uMap"), 0);
    glBindVertexArray(m->vao);
    glDrawArrays(GL_TRIANGLES, 0, m->count);
    glUseProgram(R.standard.prog);
}

void render_decal(const Mesh *m, GLuint alpha_map, const float color[3], float opacity, mat4 model)
{
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    basic_draw(m, alpha_map, true, color, opacity, model);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void render_additive(const Mesh *m, GLuint map, const float color[3], mat4 model)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    basic_draw(m, map, false, color, 1.f, model);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void render_scene_end(void)
{
    if (R.msaa_fbo) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, R.msaa_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, R.scene_fbo);
        glBlitFramebuffer(0, 0, R.scene_w, R.scene_h, 0, 0, R.scene_w, R.scene_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------- overlay ----------

static void overlay_quad(int mode, GLuint tex, float x, float y, float w, float h, const float color[4])
{
    const Program *p = &R.overlay;
    glUniform1i(uloc(p, "uMode"), mode);
    glUniform4f(uloc(p, "uRect"), x, y, w, h);
    glUniform4f(uloc(p, "uColor"), color[0], color[1], color[2], color[3]);
    bind_tex(0, tex);
    glUniform1i(uloc(p, "uTex"), 0);
    glBindVertexArray(R.quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void render_overlay_begin(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, R.win_w, R.win_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(R.overlay.prog);
    glUniform2f(uloc(&R.overlay, "uScreen"), (float) R.win_w, (float) R.win_h);
}

void render_overlay_scene(float alpha)
{
    float c[4] = { 1, 1, 1, alpha };
    glDisable(GL_BLEND);
    // the page behind the canvas while it fades in
    glClearColor(7.f / 255.f, 7.f / 255.f, 10.f / 255.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (alpha < 1.f)
        glEnable(GL_BLEND);
    overlay_quad(3, R.scene_tex, 0, 0, (float) R.win_w, (float) R.win_h, c);
    glEnable(GL_BLEND);
}

void render_overlay_scrim(float alpha)
{
    float c[4] = { 1, 1, 1, alpha };
    overlay_quad(4, 0, 0, 0, (float) R.win_w, (float) R.win_h, c);
}

void render_overlay_rect(float x, float y, float w, float h, float radius, const float fill[4], float border, const float border_color[4])
{
    const Program *p = &R.overlay;
    float none[4] = { 0, 0, 0, 0 };
    glUniform1f(uloc(p, "uRadius"), fminf(radius, fminf(w, h) / 2.f));
    glUniform1f(uloc(p, "uBorder"), border);
    const float *bc = border_color ? border_color : none;
    glUniform4f(uloc(p, "uBorderColor"), bc[0], bc[1], bc[2], bc[3]);
    overlay_quad(2, 0, x, y, w, h, fill);
}

void render_overlay_tex(GLuint tex, float x, float y, float w, float h, const float color[4], bool mask)
{
    overlay_quad(mask ? 0 : 1, tex, x, y, w, h, color);
}

void render_overlay_end(void)
{
    glDisable(GL_BLEND);
}

bool render_read_pixels(int *w, int *h, unsigned char **rgba)
{
    *w = R.win_w;
    *h = R.win_h;
    size_t row = (size_t) R.win_w * 4;
    unsigned char *buf = malloc(row * (size_t) R.win_h);
    unsigned char *out = malloc(row * (size_t) R.win_h);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, R.win_w, R.win_h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    for (int y = 0; y < R.win_h; y++)
        memcpy(out + (size_t) y * row, buf + (size_t) (R.win_h - 1 - y) * row, row);
    free(buf);
    *rgba = out;
    return true;
}
