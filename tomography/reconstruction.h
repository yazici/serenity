#pragma once
#include "project.h"

struct Reconstruction {
    Projection A; // Projection operator
    CLVolume x;
    uint k = 0;
    uint64 time = 0;
    float SSE = inf;
    float bestSSE = inf;
    int divergent = 0; // Divergent iterations
    uint64 stopTime = 0;

    Reconstruction(const Projection& A, string name) : A(A), x(cylinder(VolumeF(A.volumeSize, 0, name), 1.f/sqrt(float(sq(A.volumeSize.x)+sq(A.volumeSize.y)+sq(A.volumeSize.z))))) { assert_(x.size.x==x.size.y); }
    virtual ~Reconstruction() {}
    virtual void step() abstract;
};
inline bool operator <(const Reconstruction& a, const Reconstruction& b) { return a.time < b.time; }

inline buffer<uint> shuffleSequence(uint size) {
    buffer<uint> seq(size);
    Random random;
    for(uint i: range(size)) seq[i] = i;
    for(uint i=size-1; i>0; i--) swap(seq[i], seq[random%(i+1)]);
    return seq;
}

struct SubsetReconstruction : Reconstruction {
    const uint subsetSize, subsetCount;
    struct Subset {
        ProjectionArray At;
        ImageArray b;
    };
    buffer<Subset> subsets;

    uint subsetIndex = 0;
    buffer<uint> shuffle = shuffleSequence(subsetCount);

    SubsetReconstruction(const Projection& A, const ImageArray& b, const uint subsetSize, string name) : Reconstruction(A, name), subsetSize(subsetSize), subsetCount(A.count/subsetSize) {
        assert_(subsetCount*subsetSize == A.count);
        subsets = buffer<Subset>(subsetCount);
        for(uint subsetIndex: range(subsetCount)) {
            new (subsets+subsetIndex) Subset{ apply(subsetSize, [&](uint index){ return A.worldToDevice(interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index)); }), int3(b.size.xy(), subsetSize)};
            for(uint index: range(subsetSize)) copy(b, subsets[subsetIndex].b, int3(0,0,interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index)), int3(0,0,index), int3(b.size.xy(),1));
        }
    }
};
