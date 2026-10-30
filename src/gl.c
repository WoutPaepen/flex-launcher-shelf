#include <string.h>
#include <SDL.h>
#define FLEX_GL_NO_ALIASES
#include "gl.h"

#define X(ret, name, args) PFN_##name flex_##name = NULL;
FLEX_GL_FUNCTIONS
#undef X

bool gl_load(const char **missing)
{
#define X(ret, name, args) \
    flex_##name = (PFN_##name) SDL_GL_GetProcAddress(#name); \
    if (flex_##name == NULL) { \
        if (missing != NULL) \
            *missing = #name; \
        return false; \
    }
    FLEX_GL_FUNCTIONS
#undef X
    return true;
}

bool gl_has_extension(const char *name)
{
    GLint count = 0;
    flex_glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; i++) {
        const char *ext = (const char*) flex_glGetStringi(GL_EXTENSIONS, (GLuint) i);
        if (ext != NULL && !strcmp(ext, name))
            return true;
    }
    return false;
}
