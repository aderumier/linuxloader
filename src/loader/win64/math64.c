// libm: the CRT's, through System V entry points (see ll64.h).
#include <complex.h>
#include <fenv.h>
#include <math.h>
#include <string.h>
#include "ll64.h"

#define M1(n)                                                                                                          \
    static SYSV double lx_##n(double a) { return n(a); }                                                               \
    static SYSV float lx_##n##f(float a) { return n##f(a); }                                                           \
    static SYSV long double lx_##n##l(long double a) { return n##l(a); }
#define M2(n)                                                                                                          \
    static SYSV double lx_##n(double a, double b) { return n(a, b); }                                                  \
    static SYSV float lx_##n##f(float a, float b) { return n##f(a, b); }                                               \
    static SYSV long double lx_##n##l(long double a, long double b) { return n##l(a, b); }
#define C1(n)                                                                                                          \
    static SYSV double complex lx_##n(double complex a) { return n(a); }                                               \
    static SYSV float complex lx_##n##f(float complex a) { return n##f(a); }                                           \
    static SYSV long double complex lx_##n##l(long double complex a) { return n##l(a); }

M1(sin) M1(cos) M1(tan) M1(asin) M1(acos) M1(atan) M1(sinh) M1(cosh) M1(tanh) M1(asinh) M1(acosh) M1(atanh)
M1(exp) M1(exp2) M1(expm1) M1(log) M1(log10) M1(log2) M1(log1p) M1(sqrt) M1(cbrt) M1(ceil) M1(floor) M1(trunc)
M1(round) M1(rint) M1(nearbyint) M1(fabs) M1(erf) M1(erfc) M1(tgamma) M1(lgamma) M1(logb)
M2(atan2) M2(pow) M2(fmod) M2(hypot) M2(fmin) M2(fmax) M2(copysign) M2(remainder) M2(fdim) M2(nextafter)
C1(ccos) C1(csin) C1(cexp) C1(clog) C1(csqrt) C1(ctan)

#undef C1

static SYSV double lx_cabs(double complex a) { return cabs(a); }
static SYSV float lx_cabsf(float complex a) { return cabsf(a); }
static SYSV long double lx_cabsl(long double complex a) { return cabsl(a); }
static SYSV double lx_carg(double complex a) { return carg(a); }
static SYSV float lx_cargf(float complex a) { return cargf(a); }
static SYSV double complex lx_cpow(double complex a, double complex b) { return cpow(a, b); }
static SYSV float complex lx_cpowf(float complex a, float complex b) { return cpowf(a, b); }

static SYSV double lx_fma(double a, double b, double c) { return fma(a, b, c); }
static SYSV float lx_fmaf(float a, float b, float c) { return fmaf(a, b, c); }
static SYSV long double lx_fmal(long double a, long double b, long double c) { return fmal(a, b, c); }
static SYSV double lx_frexp(double a, int *e) { return frexp(a, e); }
static SYSV float lx_frexpf(float a, int *e) { return frexpf(a, e); }
static SYSV long double lx_frexpl(long double a, int *e) { return frexpl(a, e); }
static SYSV double lx_ldexp(double a, int e) { return ldexp(a, e); }
static SYSV float lx_ldexpf(float a, int e) { return ldexpf(a, e); }
static SYSV long double lx_ldexpl(long double a, int e) { return ldexpl(a, e); }
static SYSV double lx_modf(double a, double *i) { return modf(a, i); }
static SYSV float lx_modff(float a, float *i) { return modff(a, i); }
static SYSV long double lx_modfl(long double a, long double *i) { return modfl(a, i); }
static SYSV int64_t lx_lround(double a) { return llround(a); }
static SYSV int64_t lx_lroundf(float a) { return llroundf(a); }
static SYSV int64_t lx_lroundl(long double a) { return llroundl(a); }
static SYSV int64_t lx_lrint(double a) { return llrint(a); }
static SYSV int64_t lx_lrintf(float a) { return llrintf(a); }
static SYSV double lx_scalbn(double a, int e) { return scalbn(a, e); }
static SYSV float lx_scalbnf(float a, int e) { return scalbnf(a, e); }
static SYSV long double lx_scalbnl(long double a, int e) { return scalbnl(a, e); }
static SYSV int lx_ilogb(double a) { return ilogb(a); }
static SYSV void lx_sincos(double a, double *s, double *c) { *s = sin(a); *c = cos(a); }
static SYSV void lx_sincosf(float a, float *s, float *c) { *s = sinf(a); *c = cosf(a); }
static SYSV void lx_sincosl(long double a, long double *s, long double *c) { *s = sinl(a); *c = cosl(a); }
static SYSV double lx_jn(int n, double a) { return _jn(n, a); }
static SYSV float lx_jnf(int n, float a) { return (float)_jn(n, a); }
static SYSV long double lx_jnl(int n, long double a) { return _jn(n, (double)a); }
static SYSV double lx_yn(int n, double a) { return _yn(n, a); }
static SYSV float lx_ynf(int n, float a) { return (float)_yn(n, a); }
static SYSV long double lx_ynl(int n, long double a) { return _yn(n, (double)a); }
static SYSV double lx_j0(double a) { return _j0(a); }
static SYSV double lx_j1(double a) { return _j1(a); }
static SYSV double lx_y0(double a) { return _y0(a); }
static SYSV double lx_y1(double a) { return _y1(a); }

// glibc's classification: FP_NAN 0, FP_INFINITE 1, FP_ZERO 2, FP_SUBNORMAL 3,
// FP_NORMAL 4.
static int glibcClass(int c)
{
    switch (c)
    {
    case FP_NAN: return 0;
    case FP_INFINITE: return 1;
    case FP_ZERO: return 2;
    case FP_SUBNORMAL: return 3;
    default: return 4;
    }
}
static SYSV int lx_fpclassify(double a) { return glibcClass(fpclassify(a)); }
static SYSV int lx_fpclassifyf(float a) { return glibcClass(fpclassify(a)); }
static SYSV int lx_fpclassifyl(long double a) { return glibcClass(fpclassify(a)); }
static SYSV int lx_isinf(double a) { return isinf(a) ? (a > 0 ? 1 : -1) : 0; }
static SYSV int lx_isinff(float a) { return isinf(a) ? (a > 0 ? 1 : -1) : 0; }
static SYSV int lx_isinfl(long double a) { return isinf(a) ? (a > 0 ? 1 : -1) : 0; }
static SYSV int lx_isnan(double a) { return isnan(a); }
static SYSV int lx_isnanf(float a) { return isnan(a); }
static SYSV int lx_isnanl(long double a) { return isnan(a); }
static SYSV int lx_finite(double a) { return isfinite(a); }
static SYSV int lx_finitef(float a) { return isfinite(a); }
static SYSV int lx_signbit(double a) { return signbit(a) != 0; }
static SYSV int lx_signbitf(float a) { return signbit(a) != 0; }
static SYSV int lx_issignaling(double a) { (void)a; return 0; }
static SYSV int lx_issignalingf(float a) { (void)a; return 0; }
static SYSV int lx_issignalingl(long double a) { (void)a; return 0; }

// fenv: the SSE control word (Linux's FE_* bits are the x86 ones).
static SYSV int lx_fegetround(void) { return fegetround(); }
static SYSV int lx_fesetround(int r) { return fesetround(r); }
static SYSV int lx_feclearexcept(int e) { return feclearexcept(e); }
static SYSV int lx_fetestexcept(int e) { return fetestexcept(e); }
static SYSV int lx_feraiseexcept(int e) { return feraiseexcept(e); }
static SYSV int lx_fegetenv(void *e) { memset(e, 0, 32); return 0; }
static SYSV int lx_fesetenv(const void *e) { (void)e; return 0; }
static SYSV int lx_feholdexcept(void *e) { memset(e, 0, 32); return 0; }
static SYSV int lx_feupdateenv(const void *e) { (void)e; return 0; }

static int lxSigngam;

#define E3(n) {#n, lx_##n}, {#n "f", lx_##n##f}, {#n "l", lx_##n##l}

static const LxSym syms[] = {
    E3(sin), E3(cos), E3(tan), E3(asin), E3(acos), E3(atan), E3(sinh), E3(cosh), E3(tanh), E3(asinh), E3(acosh),
    E3(atanh), E3(exp), E3(exp2), E3(expm1), E3(log), E3(log10), E3(log2), E3(log1p), E3(sqrt), E3(cbrt), E3(ceil),
    E3(floor), E3(trunc), E3(round), E3(rint), E3(nearbyint), E3(fabs), E3(erf), E3(erfc), E3(tgamma), E3(lgamma),
    E3(logb), E3(atan2), E3(pow), E3(fmod), E3(hypot), E3(fmin), E3(fmax), E3(copysign), E3(remainder), E3(fdim),
    E3(nextafter), E3(ccos), E3(csin), E3(cexp), E3(clog), E3(csqrt), E3(ctan), E3(cabs), E3(fma), E3(frexp),
    E3(ldexp), E3(modf), E3(lround), E3(scalbn), E3(sincos), E3(jn), E3(yn),
    {"carg", lx_carg}, {"cargf", lx_cargf}, {"cpow", lx_cpow}, {"cpowf", lx_cpowf}, {"lrint", lx_lrint},
    {"lrintf", lx_lrintf}, {"llround", lx_lround}, {"llroundf", lx_lroundf}, {"llrint", lx_lrint},
    {"ilogb", lx_ilogb}, {"j0", lx_j0}, {"j1", lx_j1}, {"y0", lx_y0}, {"y1", lx_y1},
    {"__exp_finite", lx_exp}, {"__log_finite", lx_log}, {"__pow_finite", lx_pow}, {"__expf_finite", lx_expf},
    {"__logf_finite", lx_logf}, {"__powf_finite", lx_powf}, {"__atan2f_finite", lx_atan2f},
    {"__acosf_finite", lx_acosf}, {"__asinf_finite", lx_asinf}, {"__sqrtf_finite", lx_sqrtf},
    {"__fpclassify", lx_fpclassify}, {"__fpclassifyf", lx_fpclassifyf}, {"__fpclassifyl", lx_fpclassifyl},
    {"__isinf", lx_isinf}, {"__isinff", lx_isinff}, {"__isinfl", lx_isinfl}, {"isinf", lx_isinf},
    {"isinff", lx_isinff}, {"__isnan", lx_isnan}, {"__isnanf", lx_isnanf}, {"__isnanl", lx_isnanl},
    {"isnan", lx_isnan}, {"isnanf", lx_isnanf}, {"finite", lx_finite}, {"__finite", lx_finite},
    {"finitef", lx_finitef}, {"__finitef", lx_finitef}, {"__signbit", lx_signbit}, {"__signbitf", lx_signbitf},
    {"__issignaling", lx_issignaling}, {"__issignalingf", lx_issignalingf}, {"__issignalingl", lx_issignalingl},
    {"fegetround", lx_fegetround}, {"fesetround", lx_fesetround}, {"feclearexcept", lx_feclearexcept},
    {"fetestexcept", lx_fetestexcept}, {"feraiseexcept", lx_feraiseexcept}, {"fegetenv", lx_fegetenv},
    {"fesetenv", lx_fesetenv}, {"feholdexcept", lx_feholdexcept}, {"feupdateenv", lx_feupdateenv},
    {"signgam", &lxSigngam}, {"__signgam", &lxSigngam},
};

void lxMathInit(void)
{
    lxRegister(syms, LX_COUNT(syms));
}
