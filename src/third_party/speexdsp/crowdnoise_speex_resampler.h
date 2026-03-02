// Wrapper around SpeexDSP's resampler that:
// - Builds it "outside of speex" (no other SpeexDSP dependencies).
// - Uses a unique symbol prefix to avoid name clashes if you ever link the real SpeexDSP.
// - Forces floating-point mode (recommended for best quality).
//
// This header is intended to be included by C/C++ code that wants to call the resampler API.
// The implementation is compiled via `crowdnoise_speex_resampler.c`.
//
// Source: https://github.com/xiph/speexdsp
// License: BSD-style (see the headers of the vendored files).

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Compile SpeexDSP resampler in standalone mode with a unique prefix.
#ifndef OUTSIDE_SPEEX
#define OUTSIDE_SPEEX
#endif

#ifndef RANDOM_PREFIX
#define RANDOM_PREFIX crowdnoise_speex
#endif

// Use floating point math for resampling.
#ifndef FLOATING_POINT
#define FLOATING_POINT
#endif

#include "speex_resampler.h"

// NOTE:
// We intentionally do NOT undef OUTSIDE_SPEEX / RANDOM_PREFIX here.
// `speex_resampler.h` defines public API macros like:
//   #define speex_resampler_init CAT_PREFIX(RANDOM_PREFIX,_resampler_init)
// Those macros need RANDOM_PREFIX to remain defined at the call site, otherwise
// they expand to invalid symbol names like `RANDOM_PREFIX_resampler_init`.
//
// This header is a small internal dependency of our native core, so it's OK
// to keep these macros defined for translation units that include it.

#ifdef __cplusplus
}  // extern "C"
#endif

