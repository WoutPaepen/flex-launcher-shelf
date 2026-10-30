// Small vector and matrix helpers for the shelf renderer.
// Matrices are column-major (OpenGL layout), conventions follow three.js.
#pragma once
#include <math.h>
#include <string.h>

#define VM_PI 3.14159265358979323846f

typedef struct { float x, y, z; } vec3;
typedef struct { float m[16]; } mat4;

static inline vec3 v3(float x, float y, float z) { vec3 v = { x, y, z }; return v; }
static inline vec3 v3_add(vec3 a, vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline vec3 v3_sub(vec3 a, vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline vec3 v3_scale(vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline vec3 v3_cross(vec3 a, vec3 b)
{
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float v3_len(vec3 a) { return sqrtf(v3_dot(a, a)); }
static inline vec3 v3_norm(vec3 a)
{
    float l = v3_len(a);
    return l > 0.f ? v3_scale(a, 1.f / l) : a;
}

static inline mat4 m4_identity(void)
{
    mat4 r;
    memset(&r, 0, sizeof(r));
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f;
    return r;
}

static inline mat4 m4_mul(mat4 a, mat4 b)
{
    mat4 r;
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            float s = 0.f;
            for (int k = 0; k < 4; k++)
                s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    }
    return r;
}

static inline mat4 m4_translate(float x, float y, float z)
{
    mat4 r = m4_identity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

static inline mat4 m4_scale(float x, float y, float z)
{
    mat4 r = m4_identity();
    r.m[0] = x; r.m[5] = y; r.m[10] = z;
    return r;
}

// Euler rotation in three.js 'XYZ' order
static inline mat4 m4_euler_xyz(float x, float y, float z)
{
    mat4 r = m4_identity();
    float a = cosf(x), b = sinf(x), c = cosf(y), d = sinf(y), e = cosf(z), f = sinf(z);
    float ae = a * e, af = a * f, be = b * e, bf = b * f;
    r.m[0] = c * e;       r.m[4] = -c * f;      r.m[8] = d;
    r.m[1] = af + be * d; r.m[5] = ae - bf * d; r.m[9] = -b * c;
    r.m[2] = bf - ae * d; r.m[6] = be + af * d; r.m[10] = a * c;
    return r;
}

static inline mat4 m4_perspective(float fovy_deg, float aspect, float near, float far)
{
    mat4 r;
    memset(&r, 0, sizeof(r));
    float f = 1.f / tanf(fovy_deg * VM_PI / 360.f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (far + near) / (near - far);
    r.m[11] = -1.f;
    r.m[14] = 2.f * far * near / (near - far);
    return r;
}

static inline mat4 m4_ortho(float l, float r_, float b, float t, float n, float f)
{
    mat4 r = m4_identity();
    r.m[0] = 2.f / (r_ - l);
    r.m[5] = 2.f / (t - b);
    r.m[10] = -2.f / (f - n);
    r.m[12] = -(r_ + l) / (r_ - l);
    r.m[13] = -(t + b) / (t - b);
    r.m[14] = -(f + n) / (f - n);
    return r;
}

// View matrix looking from eye to target
static inline mat4 m4_look_at(vec3 eye, vec3 target, vec3 up)
{
    vec3 zaxis = v3_norm(v3_sub(eye, target));
    vec3 xaxis = v3_norm(v3_cross(up, zaxis));
    vec3 yaxis = v3_cross(zaxis, xaxis);
    mat4 r = m4_identity();
    r.m[0] = xaxis.x; r.m[4] = xaxis.y; r.m[8] = xaxis.z;
    r.m[1] = yaxis.x; r.m[5] = yaxis.y; r.m[9] = yaxis.z;
    r.m[2] = zaxis.x; r.m[6] = zaxis.y; r.m[10] = zaxis.z;
    r.m[12] = -v3_dot(xaxis, eye);
    r.m[13] = -v3_dot(yaxis, eye);
    r.m[14] = -v3_dot(zaxis, eye);
    return r;
}

static inline vec3 m4_transform_point(mat4 a, vec3 p)
{
    return v3(a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z + a.m[12],
              a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z + a.m[13],
              a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z + a.m[14]);
}

static inline vec3 m4_transform_dir(mat4 a, vec3 p)
{
    return v3(a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z,
              a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z,
              a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z);
}

// Normal matrix (inverse transpose of the upper 3x3), column-major 3x3
static inline void m4_normal_matrix(mat4 a, float out[9])
{
    float m00 = a.m[0], m01 = a.m[4], m02 = a.m[8];
    float m10 = a.m[1], m11 = a.m[5], m12 = a.m[9];
    float m20 = a.m[2], m21 = a.m[6], m22 = a.m[10];
    float c00 = m11 * m22 - m12 * m21, c01 = m12 * m20 - m10 * m22, c02 = m10 * m21 - m11 * m20;
    float det = m00 * c00 + m01 * c01 + m02 * c02;
    float id = det != 0.f ? 1.f / det : 0.f;
    // inverse transpose == cofactor matrix / det, written column by column
    out[0] = c00 * id;
    out[1] = (m02 * m21 - m01 * m22) * id;
    out[2] = (m01 * m12 - m02 * m11) * id;
    out[3] = c01 * id;
    out[4] = (m00 * m22 - m02 * m20) * id;
    out[5] = (m02 * m10 - m00 * m12) * id;
    out[6] = c02 * id;
    out[7] = (m01 * m20 - m00 * m21) * id;
    out[8] = (m00 * m11 - m01 * m10) * id;
}

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float mixf(float a, float b, float t) { return a + (b - a) * t; }

// sRGB 0..255 component to linear 0..1
static inline float srgb_to_linear(float c)
{
    c /= 255.f;
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}
