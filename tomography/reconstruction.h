#pragma once
#include "project.h"
#include "random.h"

/// Reconstruction base class
struct Reconstruction {
    Projection A; // Projection settings
    CLVolume x; // Current reconstruction estimate
    uint64 time = 0; // Cumulated OpenCL kernel time (when enabled)

    /// Initializes reconstruction for projection settings \a A
    /// \note Estimate \a x is initialized with an uniform estimate 1/√(x²+y²+z²) on the cylinder support
    Reconstruction(const Projection& A, string name) : A(A), x(cylinder(VolumeF(A.volumeSize, 0, name), 1.f/sqrt(float(sq(A.volumeSize.x)+sq(A.volumeSize.y)+sq(A.volumeSize.z))))) { assert_(x.size.x==x.size.y); }
    virtual ~Reconstruction() {}
    virtual void step() abstract;
};

struct SubsetReconstruction : Reconstruction {
    const uint subsetSize, subsetCount;
    struct Subset {
        CLBuffer<mat4> At;
        ImageArray b;
    };
    array<Subset> subsets;

    uint subsetIndex = 0;
    buffer<uint> shuffle = shuffleSequence(subsetCount);

    SubsetReconstruction(const Projection& A, const ImageArray& b, const uint subsetSize, string name) : Reconstruction(A, name), subsetSize(subsetSize), subsetCount(A.count/subsetSize) {
        assert_(subsetCount*subsetSize == A.count);
        subsets.reserve(subsetCount);
        for(uint subsetIndex: range(subsetCount)) {
            subsets << Subset{ CLBuffer<mat4>(apply(subsetSize, [&](uint index){ return A.worldToDevice(interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index)); }), "A"_), ImageArray(int3(b.size.xy(), subsetSize), 0, "b"_)};
            for(uint index: range(subsetSize)) copy(b, subsets[subsetIndex].b, int3(0,0,interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index)), int3(0,0,index), int3(b.size.xy(),1));
        }
    }
};

