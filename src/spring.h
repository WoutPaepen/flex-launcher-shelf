// Damped springs drive every motion on the shelf.
// They are stepped at a fixed 240 Hz so motion is identical at any frame rate.
#pragma once
#include <math.h>
#include <stdbool.h>

#define SPRING_STEP (1.0f / 240.0f)
#define SPRING_MAX_DT 0.05f

typedef struct {
    float v;    // current value
    float t;    // target
    float vel;
    float k;    // stiffness
    float c;    // damping
} Spring;

static inline Spring spring_make(float v, float k, float zeta)
{
    Spring s = { v, v, 0.f, k, 2.f * zeta * sqrtf(k) };
    return s;
}

static inline void spring_step(Spring *s, float dt)
{
    float a = s->k * (s->t - s->v) - s->c * s->vel;
    s->vel += a * dt;
    s->v += s->vel * dt;
}

static inline bool spring_rest(const Spring *s)
{
    return fabsf(s->t - s->v) < 1e-4f && fabsf(s->vel) < 1e-4f;
}

static inline void spring_snap(Spring *s)
{
    s->v = s->t;
    s->vel = 0.f;
}
