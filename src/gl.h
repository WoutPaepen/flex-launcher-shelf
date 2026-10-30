// Minimal OpenGL 3.3 core / OpenGL ES 3.0 loader.
// Every entry point is fetched at runtime with SDL_GL_GetProcAddress,
// so the launcher does not link against any GL library directly.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_VENDOR 0x1F00
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_EXTENSIONS 0x1F03
#define GL_NUM_EXTENSIONS 0x821D

#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_BLEND 0x0BE2
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_SCISSOR_TEST 0x0C11
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_BACK 0x0405
#define GL_FRONT 0x0404
#define GL_CCW 0x0901
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303

#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_HALF_FLOAT 0x140B
#define GL_RED 0x1903
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_R8 0x8229
#define GL_RGB8 0x8051
#define GL_RGBA8 0x8058
#define GL_SRGB8_ALPHA8 0x8C43
#define GL_RGBA16F 0x881A
#define GL_RGB16F 0x881B
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH24_STENCIL8 0x88F0

#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE5 0x84C5
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05

#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8

#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84

#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_MAX_SAMPLES 0x8D57

// name, return type, argument list
#define FLEX_GL_FUNCTIONS \
    X(const GLubyte *, glGetString, (GLenum name)) \
    X(const GLubyte *, glGetStringi, (GLenum name, GLuint index)) \
    X(void, glGetIntegerv, (GLenum pname, GLint *data)) \
    X(void, glGetFloatv, (GLenum pname, GLfloat *data)) \
    X(GLenum, glGetError, (void)) \
    X(void, glEnable, (GLenum cap)) \
    X(void, glDisable, (GLenum cap)) \
    X(void, glBlendFunc, (GLenum sfactor, GLenum dfactor)) \
    X(void, glBlendFuncSeparate, (GLenum sr, GLenum dr, GLenum sa, GLenum da)) \
    X(void, glDepthMask, (GLboolean flag)) \
    X(void, glDepthFunc, (GLenum func)) \
    X(void, glCullFace, (GLenum mode)) \
    X(void, glPolygonOffset, (GLfloat factor, GLfloat units)) \
    X(void, glColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a)) \
    X(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    X(void, glScissor, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    X(void, glClear, (GLbitfield mask)) \
    X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
    X(void, glFinish, (void)) \
    X(void, glGenTextures, (GLsizei n, GLuint *textures)) \
    X(void, glDeleteTextures, (GLsizei n, const GLuint *textures)) \
    X(void, glBindTexture, (GLenum target, GLuint texture)) \
    X(void, glActiveTexture, (GLenum texture)) \
    X(void, glTexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *pixels)) \
    X(void, glTexSubImage2D, (GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void *pixels)) \
    X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param)) \
    X(void, glTexParameterf, (GLenum target, GLenum pname, GLfloat param)) \
    X(void, glGenerateMipmap, (GLenum target)) \
    X(void, glPixelStorei, (GLenum pname, GLint param)) \
    X(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels)) \
    X(void, glGenBuffers, (GLsizei n, GLuint *buffers)) \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint *buffers)) \
    X(void, glBindBuffer, (GLenum target, GLuint buffer)) \
    X(void, glBufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage)) \
    X(void, glGenVertexArrays, (GLsizei n, GLuint *arrays)) \
    X(void, glDeleteVertexArrays, (GLsizei n, const GLuint *arrays)) \
    X(void, glBindVertexArray, (GLuint array)) \
    X(void, glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)) \
    X(void, glEnableVertexAttribArray, (GLuint index)) \
    X(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count)) \
    X(GLuint, glCreateShader, (GLenum type)) \
    X(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)) \
    X(void, glCompileShader, (GLuint shader)) \
    X(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint *params)) \
    X(void, glGetShaderInfoLog, (GLuint shader, GLsizei max, GLsizei *length, GLchar *log)) \
    X(void, glDeleteShader, (GLuint shader)) \
    X(GLuint, glCreateProgram, (void)) \
    X(void, glAttachShader, (GLuint program, GLuint shader)) \
    X(void, glBindAttribLocation, (GLuint program, GLuint index, const GLchar *name)) \
    X(void, glLinkProgram, (GLuint program)) \
    X(void, glGetProgramiv, (GLuint program, GLenum pname, GLint *params)) \
    X(void, glGetProgramInfoLog, (GLuint program, GLsizei max, GLsizei *length, GLchar *log)) \
    X(void, glUseProgram, (GLuint program)) \
    X(GLint, glGetUniformLocation, (GLuint program, const GLchar *name)) \
    X(void, glUniform1i, (GLint location, GLint v0)) \
    X(void, glUniform1f, (GLint location, GLfloat v0)) \
    X(void, glUniform2f, (GLint location, GLfloat v0, GLfloat v1)) \
    X(void, glUniform3f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2)) \
    X(void, glUniform4f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)) \
    X(void, glUniformMatrix3fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)) \
    X(void, glUniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)) \
    X(void, glGenFramebuffers, (GLsizei n, GLuint *framebuffers)) \
    X(void, glDeleteFramebuffers, (GLsizei n, const GLuint *framebuffers)) \
    X(void, glBindFramebuffer, (GLenum target, GLuint framebuffer)) \
    X(void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
    X(void, glFramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum rbtarget, GLuint renderbuffer)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum target)) \
    X(void, glGenRenderbuffers, (GLsizei n, GLuint *renderbuffers)) \
    X(void, glDeleteRenderbuffers, (GLsizei n, const GLuint *renderbuffers)) \
    X(void, glBindRenderbuffer, (GLenum target, GLuint renderbuffer)) \
    X(void, glRenderbufferStorage, (GLenum target, GLenum internalformat, GLsizei w, GLsizei h)) \
    X(void, glRenderbufferStorageMultisample, (GLenum target, GLsizei samples, GLenum internalformat, GLsizei w, GLsizei h)) \
    X(void, glBlitFramebuffer, (GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter))

#define X(ret, name, args) typedef ret (*PFN_##name) args; extern PFN_##name flex_##name;
FLEX_GL_FUNCTIONS
#undef X

#ifndef FLEX_GL_NO_ALIASES
#define glGetString flex_glGetString
#define glGetStringi flex_glGetStringi
#define glGetIntegerv flex_glGetIntegerv
#define glGetFloatv flex_glGetFloatv
#define glGetError flex_glGetError
#define glEnable flex_glEnable
#define glDisable flex_glDisable
#define glBlendFunc flex_glBlendFunc
#define glBlendFuncSeparate flex_glBlendFuncSeparate
#define glDepthMask flex_glDepthMask
#define glDepthFunc flex_glDepthFunc
#define glCullFace flex_glCullFace
#define glPolygonOffset flex_glPolygonOffset
#define glColorMask flex_glColorMask
#define glViewport flex_glViewport
#define glScissor flex_glScissor
#define glClear flex_glClear
#define glClearColor flex_glClearColor
#define glFinish flex_glFinish
#define glGenTextures flex_glGenTextures
#define glDeleteTextures flex_glDeleteTextures
#define glBindTexture flex_glBindTexture
#define glActiveTexture flex_glActiveTexture
#define glTexImage2D flex_glTexImage2D
#define glTexSubImage2D flex_glTexSubImage2D
#define glTexParameteri flex_glTexParameteri
#define glTexParameterf flex_glTexParameterf
#define glGenerateMipmap flex_glGenerateMipmap
#define glPixelStorei flex_glPixelStorei
#define glReadPixels flex_glReadPixels
#define glGenBuffers flex_glGenBuffers
#define glDeleteBuffers flex_glDeleteBuffers
#define glBindBuffer flex_glBindBuffer
#define glBufferData flex_glBufferData
#define glGenVertexArrays flex_glGenVertexArrays
#define glDeleteVertexArrays flex_glDeleteVertexArrays
#define glBindVertexArray flex_glBindVertexArray
#define glVertexAttribPointer flex_glVertexAttribPointer
#define glEnableVertexAttribArray flex_glEnableVertexAttribArray
#define glDrawArrays flex_glDrawArrays
#define glCreateShader flex_glCreateShader
#define glShaderSource flex_glShaderSource
#define glCompileShader flex_glCompileShader
#define glGetShaderiv flex_glGetShaderiv
#define glGetShaderInfoLog flex_glGetShaderInfoLog
#define glDeleteShader flex_glDeleteShader
#define glCreateProgram flex_glCreateProgram
#define glAttachShader flex_glAttachShader
#define glBindAttribLocation flex_glBindAttribLocation
#define glLinkProgram flex_glLinkProgram
#define glGetProgramiv flex_glGetProgramiv
#define glGetProgramInfoLog flex_glGetProgramInfoLog
#define glUseProgram flex_glUseProgram
#define glGetUniformLocation flex_glGetUniformLocation
#define glUniform1i flex_glUniform1i
#define glUniform1f flex_glUniform1f
#define glUniform2f flex_glUniform2f
#define glUniform3f flex_glUniform3f
#define glUniform4f flex_glUniform4f
#define glUniformMatrix3fv flex_glUniformMatrix3fv
#define glUniformMatrix4fv flex_glUniformMatrix4fv
#define glGenFramebuffers flex_glGenFramebuffers
#define glDeleteFramebuffers flex_glDeleteFramebuffers
#define glBindFramebuffer flex_glBindFramebuffer
#define glFramebufferTexture2D flex_glFramebufferTexture2D
#define glFramebufferRenderbuffer flex_glFramebufferRenderbuffer
#define glCheckFramebufferStatus flex_glCheckFramebufferStatus
#define glGenRenderbuffers flex_glGenRenderbuffers
#define glDeleteRenderbuffers flex_glDeleteRenderbuffers
#define glBindRenderbuffer flex_glBindRenderbuffer
#define glRenderbufferStorage flex_glRenderbufferStorage
#define glRenderbufferStorageMultisample flex_glRenderbufferStorageMultisample
#define glBlitFramebuffer flex_glBlitFramebuffer
#endif

// Load every entry point. Returns false and names the first missing one on failure.
bool gl_load(const char **missing);
bool gl_has_extension(const char *name);
