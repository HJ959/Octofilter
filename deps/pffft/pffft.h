/* Copyright (c) 2013 Julien Pommier ( pommier@modartt.com )
   Based on original fortran 77 code from FFTPACKv4 from NETLIB,
   authored by Dr Paul Swarztrauber of NCAR, in 1985.

   FFTPACK license applies. BSD-like. See pffft.c for full text.

   PFFFT : a Pretty Fast FFT.
   Restrictions:
   - 1D transforms only, with 32-bit single precision.
   - supports only transforms for inputs of length N of the form
     N=(2^a)*(3^b)*(5^c), a >= 5, b >=0, c >= 0
   - all float* pointers must be 16-byte aligned (use pffft_aligned_malloc)
*/
#ifndef PFFFT_H
#define PFFFT_H

#include <stddef.h> // for size_t

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PFFFT_Setup PFFFT_Setup;
typedef enum { PFFFT_FORWARD, PFFFT_BACKWARD } pffft_direction_t;
typedef enum { PFFFT_REAL, PFFFT_COMPLEX } pffft_transform_t;

PFFFT_Setup *pffft_new_setup(int N, pffft_transform_t transform);
void         pffft_destroy_setup(PFFFT_Setup *);

void pffft_transform(PFFFT_Setup *setup, const float *input, float *output,
                     float *work, pffft_direction_t direction);

void pffft_transform_ordered(PFFFT_Setup *setup, const float *input,
                              float *output, float *work,
                              pffft_direction_t direction);

void pffft_zreorder(PFFFT_Setup *setup, const float *input, float *output,
                    pffft_direction_t direction);

void pffft_zconvolve_accumulate(PFFFT_Setup *setup, const float *dft_a,
                                const float *dft_b, float *dft_ab,
                                float scaling);

void *pffft_aligned_malloc(size_t nb_bytes);
void  pffft_aligned_free(void *);

int pffft_simd_size(void);

#ifdef __cplusplus
}
#endif

#endif // PFFFT_H
