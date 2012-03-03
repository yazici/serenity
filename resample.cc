/* Copyright (C) 2007-2008 Jean-Marc Valin, Thorvald Natvig

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

   1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
   IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
   OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
   HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
   STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
*/
#include "resample.h"
#include <math.h>

typedef float __m128 __attribute__ ((__vector_size__ (16), __may_alias__));
#define _mm_add_ps __builtin_ia32_addps
#define _mm_mul_ps __builtin_ia32_mulps
#define _mm_loadu_ps __builtin_ia32_loadups
#define _mm_movehl_ps __builtin_ia32_movhlps
#define _mm_add_ss __builtin_ia32_addss
#define _mm_shuffle_ps __builtin_ia32_shufps

static inline float inner_product_single(const float *a, const float *b, int len) {
    __m128 sum = {0,0,0,0};
    for(int i=0;i<len;i+=8) {
        sum = _mm_add_ps(sum, _mm_mul_ps(_mm_loadu_ps(a+i), _mm_loadu_ps(b+i)));
        sum = _mm_add_ps(sum, _mm_mul_ps(_mm_loadu_ps(a+i+4), _mm_loadu_ps(b+i+4)));
    }
    sum = _mm_add_ps(sum, _mm_movehl_ps(sum, sum));
    sum = _mm_add_ss(sum, _mm_shuffle_ps(sum, sum, 0x55));
    return __builtin_ia32_vec_ext_v4sf(sum, 0);
}

const int filterSize = 256;
const float bandwidth = 0.975;
const int windowOversample = 64;
static double kaiser12[68] = { //TODO: generate
    0.99859849, 1.00000000, 0.99859849, 0.99440475, 0.98745105, 0.97779076, 0.96549770, 0.95066529,
    0.93340547, 0.91384741, 0.89213598, 0.86843014, 0.84290116, 0.81573067, 0.78710866, 0.75723148,
    0.72629970, 0.69451601, 0.66208321, 0.62920216, 0.59606986, 0.56287762, 0.52980938, 0.49704014,
    0.46473455, 0.43304576, 0.40211431, 0.37206735, 0.34301800, 0.31506490, 0.28829195, 0.26276832,
    0.23854851, 0.21567274, 0.19416736, 0.17404546, 0.15530766, 0.13794294, 0.12192957, 0.10723616,
    0.09382272, 0.08164178, 0.07063950, 0.06075685, 0.05193064, 0.04409466, 0.03718069, 0.03111947,
    0.02584161, 0.02127838, 0.01736250, 0.01402878, 0.01121463, 0.00886058, 0.00691064, 0.00531256,
    0.00401805, 0.00298291, 0.00216702, 0.00153438, 0.00105297, 0.00069463, 0.00043489, 0.00025272,
    0.00013031, 0.0000527734, 0.00001000, 0.00000000 };

static double window(float x) {
    float y = x*windowOversample;
    int i = floor(y);
    float t = (y-i);
    double interp[4];
    interp[3] =  -t/6 + (t*t*t)/6;
    interp[2] = t + (t*t)/2 - (t*t*t)/2;
    interp[0] = -t/3 + (t*t)/2 - (t*t*t)/6;
    interp[1] = 1-interp[3]-interp[2]-interp[0];
    return interp[0]*kaiser12[i] + interp[1]*kaiser12[i+1] + interp[2]*kaiser12[i+2] + interp[3]*kaiser12[i+3];
}
static float sinc(double cutoff, double x, int N) {
    if (fabs(x)<1e-6) return cutoff;
    else if (fabs(x) > N/2.0) return 0;
    double xx = x * cutoff;
    return cutoff*sin(M_PI*xx)/(M_PI*xx) * window(fabs(2*x/N));
}

/// Returns the largest positive integer that divides the numbers without a remainder
inline int gcd(int a, int b) { while(b != 0) { int t = b; b = a % b; a = t; } return a; }

Resampler::Resampler(int channelCount, int sourceRate, int targetRate) : channelCount(channelCount) {
    int factor = gcd(sourceRate,targetRate);
    this->sourceRate = sourceRate/=factor;
    this->targetRate = targetRate/=factor;


    float cutoff;
    if (sourceRate > targetRate) { //downsampling
        cutoff = bandwidth * targetRate / sourceRate;
        N = filterSize * sourceRate / targetRate;
        N &= (~0x3); // Round down to make sure we have a multiple of 4
    } else { //upsampling
        cutoff = bandwidth;
        N = filterSize;
    }

    kernel = new float[N*targetRate];
    for(int i=0;i<targetRate;i++) {
        for (int j=0;j<N;j++) {
            kernel[i*N+j] = sinc(cutoff, (j-N/2+1)-float(i)/targetRate, N);
        }
    }

    integerAdvance = sourceRate/targetRate;
    decimalAdvance = sourceRate%targetRate;

    const int bufferSize=160;
    memSize = N-1+bufferSize;
    mem = new float[channelCount*memSize];
    clear(mem,channelCount*memSize);
}
Resampler::operator bool() { return kernel; }
Resampler::~Resampler() { delete[] mem; mem=0; delete[] kernel; kernel=0; }

void Resampler::filter(const float* source, int *sourceSize, float* target, int *targetSize) {
    int ilen=0, olen=0;
    for (int channel=0;channel<channelCount;channel++) {
        const float* in = source+channel;
        float* out = target+channel;
        ilen = *sourceSize;
        olen = *targetSize;
        while (ilen && olen) {
            const int filterOffset = N - 1;
            const int xlen = memSize - filterOffset;
            int ichunk = (ilen > xlen) ? xlen : ilen;
            int ochunk = olen;

            float *x = mem + channel * memSize; //TODO: avoid copy (interleaved sinc lookup)
            for(int j=0;j<ichunk;++j) x[j+filterOffset]=in[j*channelCount];

            int targetIndex = 0;
            int& integerIndex = channels[channel].integerIndex;
            int& decimalIndex = channels[channel].decimalIndex;

            while (!(integerIndex >= (int)ichunk || targetIndex >= (int)ochunk)) {
                const float *sinc = & kernel[decimalIndex*N];
                const float *iptr = & x[integerIndex];

                out[channelCount * targetIndex++] = inner_product_single(sinc, iptr, N);
                integerIndex += integerAdvance;
                decimalIndex += decimalAdvance;
                if (decimalIndex >= targetRate) {
                    decimalIndex -= targetRate;
                    integerIndex++;
                }
            }

            if (integerIndex < (int)ichunk) ichunk = integerIndex;
            ochunk = targetIndex;
            integerIndex -= ichunk;

            for(int j=0;j<N-1;++j) x[j] = x[j+ichunk];

            ilen -= ichunk;
            olen -= ochunk;
            out += ochunk * channelCount;
            in += ichunk * channelCount;
        }
    }
    *sourceSize -= ilen;
    *targetSize -= olen;
}
