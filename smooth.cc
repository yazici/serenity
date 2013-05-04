#include "smooth.h"
#include "simd.h"

void shiftRight(Volume16& target, const Volume16& source, uint shift) {
    const uint16* const src = source;
    uint16* const dst = target;
    uint64 size = source.size();
    for(uint i=0; i<size; i+=8) storea(dst+i, shiftRight(loada(src+i), shift));
}

void smooth(Volume16& target, const Volume16& source, uint X, uint Y, uint Z, uint size, uint shift) {
    const uint16* const sourceData = source;
    uint16* const targetData = target;
    const uint margin = align(4,size)-size;
    const uint XY = X*Y;
    for(uint z=0; z<Z; z++) for(uint x=0; x<X; x+=16) {
        const uint16* const sourceZX = sourceData+z*XY+x;
        uint16* const targetXZ = targetData+x*XY+z*X;
        v8hi sum[2] = {}; //16×16i instructions would need AVX2
        for(uint y=margin; y<2*size+margin; y++) for(uint i=0; i<2; i++) sum[i] += loada(sourceZX+y*X+i*8);
        for(uint y=margin+size; y<Y-size-margin; y+=8) {
            for(uint i=0; i<2; i++) { // Avoid reloading L1 cache lines twice (L1 = 1024 lines of 16 samples) but transpose one 8x8 tile at a time (SSE = 16 registers of 8 samples)
                v8hi tile[8];
                const uint16* const sourceZYX = sourceZX+y*X+i*8;
                for(uint dy=0; dy<8; dy++) {
                    sum[i] += loada(sourceZYX+int(dy+size)*int(X));
                    tile[dy] = shiftRight(sum[i], shift); // Rescales intermediate results to limit necessary headroom
                    sum[i] -= loada(sourceZYX+int(dy-size)*int(X));
                }
                transpose8(targetXZ+i*8*XY+y, XY, tile[0], tile[1], tile[2], tile[3], tile[4], tile[5], tile[6], tile[7]);
            }
        }
    }
}
