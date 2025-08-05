#ifndef _GAME_MATH_H_
#define _GAME_MATH_H_

#include <stdint.h>
#include <stdbool.h>

// Fixed-point types
typedef int32_t fixed16_t;
typedef int64_t fixed32_t;

#define FIXED16_ONE   65536
#define FIXED16_HALF  32768
#define FIXED16_QUARTER 16384
#define FIXED16_TWO   131072

#define FIXED32_ONE   4294967296LL
#define FIXED32_HALF  2147483648LL

// Conversion macros
#define FLOAT_TO_FIXED16(f) ((fixed16_t)((f) * FIXED16_ONE))
#define FIXED16_TO_FLOAT(f) ((float)(f) / FIXED16_ONE)
#define INT_TO_FIXED16(i) ((fixed16_t)((i) * FIXED16_ONE))
#define FIXED16_TO_INT(f) ((int32_t)((f) >> 16))

// Trigonometry constants
#define SIN_TABLE_SIZE 1024
#define SIN_TABLE_MASK (SIN_TABLE_SIZE - 1)

// Trigonometry functions
extern const int16_t sin_table[SIN_TABLE_SIZE];
extern const int16_t cos_table[SIN_TABLE_SIZE];

static inline fixed16_t fixed_sin(fixed16_t angle) {
    int32_t index = (angle * SIN_TABLE_SIZE) / (2 * FIXED16_ONE);
    index &= SIN_TABLE_MASK;
    return (fixed16_t)sin_table[index] << 2;
}

static inline fixed16_t fixed_cos(fixed16_t angle) {
    int32_t index = (angle * SIN_TABLE_SIZE) / (2 * FIXED16_ONE);
    index = (index + SIN_TABLE_SIZE / 4) & SIN_TABLE_MASK;
    return (fixed16_t)cos_table[index] << 2;
}

// Basic arithmetic
static inline fixed16_t fixed_mul(fixed16_t a, fixed16_t b) {
    return (fixed16_t)(((int64_t)a * b) >> 16);
}

static inline fixed16_t fixed_div(fixed16_t a, fixed16_t b) {
    return (fixed16_t)(((int64_t)a << 16) / b);
}

static inline fixed16_t fixed_sqrt(fixed16_t x) {
    if (x <= 0) return 0;
    
    fixed16_t result = x;
    fixed16_t temp = 0;
    
    for (int i = 0; i < 16; i++) {
        temp = fixed_div(x, result);
        result = (result + temp) >> 1;
    }
    
    return result;
}

// 2D vector operations
typedef struct {
    fixed16_t x, y;
} vec2_t;

typedef struct {
    fixed16_t x, y, z;
} vec3_t;

static inline vec2_t vec2_add(vec2_t a, vec2_t b) {
    return (vec2_t){a.x + b.x, a.y + b.y};
}

static inline vec2_t vec2_sub(vec2_t a, vec2_t b) {
    return (vec2_t){a.x - b.x, a.y - b.y};
}

static inline vec2_t vec2_scale(vec2_t v, fixed16_t s) {
    return (vec2_t){fixed_mul(v.x, s), fixed_mul(v.y, s)};
}

static inline fixed16_t vec2_dot(vec2_t a, vec2_t b) {
    return fixed_mul(a.x, b.x) + fixed_mul(a.y, b.y);
}

static inline fixed16_t vec2_length(vec2_t v) {
    return fixed_sqrt(fixed_mul(v.x, v.x) + fixed_mul(v.y, v.y));
}

// 3D vector operations
static inline vec3_t vec3_add(vec3_t a, vec3_t b) {
    return (vec3_t){a.x + b.x, a.y + b.y, a.z + b.z};
}

static inline vec3_t vec3_scale(vec3_t v, fixed16_t s) {
    return (vec3_t){fixed_mul(v.x, s), fixed_mul(v.y, s), fixed_mul(v.z, s)};
}

// Matrix operations
typedef struct {
    fixed16_t m[3][3];
} mat3_t;

void mat3_identity(mat3_t *m);
void mat3_rotation(mat3_t *m, fixed16_t angle);
void mat3_multiply(mat3_t *result, const mat3_t *a, const mat3_t *b);
vec2_t mat3_transform(const mat3_t *m, vec2_t v);

// Fast inverse square root approximation
static inline fixed16_t fixed_rsqrt(fixed16_t x) {
    if (x <= 0) return FIXED16_ONE;
    
    int32_t i = 0x5F3759DF - (((int32_t)x) >> 1);
    fixed16_t y = *(float *)&i;
    y = fixed_mul(y, FIXED16_ONE - fixed_mul(fixed_mul(x, y), y) / 2);
    return y;
}

#endif // _GAME_MATH_H_