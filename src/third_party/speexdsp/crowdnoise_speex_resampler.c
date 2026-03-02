// Standalone build unit for the SpeexDSP resampler.
//
// IMPORTANT:
// - Add this .c file to your native build (Android NDK + CMake, and Xcode).
// - This compiles `resample.c` with the required defines (standalone + floating point).

#define OUTSIDE_SPEEX
#define RANDOM_PREFIX crowdnoise_speex
#define FLOATING_POINT

// Compile the resampler implementation.
#include "resample.c"

