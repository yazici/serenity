#include "operators.h"

static float reduce1(CLKernel& kernel, const CLVolume& A, const int3 origin=0, int3 size=0) {
    size = size?:A.size;
    CLBufferF buffer (size.x*size.y*size.z); copy(buffer, A, origin, size);
    size_t elementCount = size.z*size.y*size.x;
    size_t blockSize = 128; // threadCount
    assert_(elementCount % blockSize == 0); //FIXME
    size_t blockCount = elementCount / blockSize;
    CLBufferF output (blockCount);
    kernel.localSpace = blockSize*sizeof(float);
    kernel(blockCount, blockSize, buffer, output, elementCount);
    float blockSums[blockCount];
    output.read(mref<float>(blockSums,blockCount));
    float sum = ::sum(ref<float>(blockSums,blockCount));
    assert_(isNumber(sum), kernel.name);
    return sum;
}

CL(sum, sum) float sum(const CLVolume& A, const int3 origin, const int3 size) { return reduce1(CL::sum, A, origin, size); }
CL(sum, SSQ) float SSQ(const CLVolume& A, const int3 origin, const int3 size) { return reduce1(CL::SSQ, A, origin, size); }

static float reduce2(CLKernel& kernel, const CLVolume& A, const CLVolume& B, const int3 origin=0, int3 size=0) {
    size = size?:A.size;
    assert_(A.size == B.size);
    CLBufferF Abuffer (size.x*size.y*size.z); copy(Abuffer, A, origin, size);
    CLBufferF Bbuffer (size.x*size.y*size.z); copy(Bbuffer, B, origin, size);
    size_t elementCount = size.z*size.y*size.x;
    size_t blockSize = 128; // threadCount
    assert_(elementCount % blockSize == 0); //FIXME
    size_t blockCount = elementCount / blockSize;
    CLBufferF output (blockCount);
    kernel.localSpace = blockSize*sizeof(float);
    kernel(blockCount, blockSize, Abuffer, Bbuffer, output, elementCount);
    float blockSums[blockCount];
    output.read(mref<float>(blockSums,blockCount));
    float sum = ::sum(ref<float>(blockSums,blockCount));
    assert_(isNumber(sum), kernel.name);
    return sum;
}

CL(sum, SSE)  float SSE(const CLVolume& A, const CLVolume& B, const int3 origin, const int3 size) { return reduce2(CL::SSE, A, B, origin, size); }
CL(sum, dotProduct)  float dotProduct(const CLVolume& A, const CLVolume& B, const int3 origin, const int3 size) { return reduce2(CL::dotProduct, A, B, origin, size); }