#pragma once
typedef double real;

inline bool isNaN(float x) { return __builtin_isnanf(x); }

inline float floor(float x) { return __builtin_floorf(x); }
inline real floor(real x) { return __builtin_floor(x); }
inline float round(float x) { return __builtin_roundf(x); }
inline real round(real x) { return __builtin_round(x); }
inline float ceil(float x) { return __builtin_ceilf(x); }
inline real ceil(real x) { return __builtin_ceil(x); }
inline real mod(real q, real d) { return __builtin_fmod(q, d); }
inline float sqrt(float f) { return __builtin_sqrtf(f); }
inline real sqrt(real f) { return __builtin_sqrt(f); }
inline real pow(real x, real y) { return __builtin_pow(x,y); }

inline real exp(real x) { return __builtin_exp(x); }
inline real ln(real x) { return __builtin_log(x); }

inline real cos(real t) { return __builtin_cos(t); }
inline real acos(real t) { return __builtin_acos(t); }
inline real sin(real t) { return __builtin_sin(t); }
inline real asin(real t) { return __builtin_asin(t); }
inline real tan(real t) { return __builtin_tan(t); }
inline real atan(real y, real x) { return __builtin_atan2(y, x); }

const real PI = 3.14159265358979323846;
inline real rad(real t) { return t/180*PI; }
inline real deg(real t) { return t/PI*180; }
