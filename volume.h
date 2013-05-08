#pragma once
#include "image.h"

struct Volume {
    Volume(){}

    uint64 size() const { return x*y*z; }
    explicit operator bool() const { return data; }
    operator const struct Volume8&() const { assert_(sampleSize==sizeof(uint8),sampleSize); return (struct Volume8&)*this; }
    operator const struct Volume16&() const { assert_(sampleSize==sizeof(uint16),sampleSize); return (struct Volume16&)*this; }
    operator const struct Volume32&() const { assert_(sampleSize==sizeof(uint32),sampleSize); return (struct Volume32&)*this; }
    operator struct Volume8&() { assert_(sampleSize==sizeof(uint8),sampleSize); return *(struct Volume8*)this; }
    operator struct Volume16&() { assert_(sampleSize==sizeof(uint16),sampleSize); return *(struct Volume16*)this; }
    operator struct Volume32&() { assert_(sampleSize==sizeof(uint32),sampleSize); return *(struct Volume32*)this; }
    void copyMetadata(const Volume& source) { marginX=source.marginX, marginY=source.marginY, marginZ=source.marginZ; num=source.num, den=source.den; squared=source.squared; }

    buffer<byte> data; // Samples ordered in Z slices, Y rows, X samples
    buffer<uint> offsetX, offsetY, offsetZ; // Offset lookup tables for bricked volumes
    uint x=0, y=0, z=0; // Sample count in each dimensions
    uint marginX=0, marginY=0, marginZ=0; // Margins to trim when processing volume
    uint num=1, den=1; // Scale to apply to compute normalized values (data*numerator/denominator)
    uint sampleSize=0; // Sample integer size (in bytes)
    bool squared=false; // Whether the sample are a squared magnitude
};

/// Serializes volume format (size, margin, range, layout)
string volumeFormat(const Volume& volume);
/// Parses volume format (i.e sample format)
void parseVolumeFormat(Volume& volume, const ref<byte>& path);

struct Volume8 : Volume {
    typedef uint8 T;
    operator const T*() const { return (T*)data.data; }
    operator T*() { return (T*)data.data; }
};

struct Volume16 : Volume {
    typedef uint16 T;
    operator const T*() const { return (T*)data.data; }
    operator T*() { return (T*)data.data; }
};

struct Volume32 : Volume {
    typedef uint32 T;
    operator const T*() const { return (T*)data.data; }
    operator T*() { return (T*)data.data; }
};

/// Returns maximum of data
uint maximum(const Volume16& source);
uint maximum(const Volume32& source);

/// Converts a 32bit volume to 16bit
void pack(Volume16& target, const Volume32& source);

/// Generates lookup tables for tiled volume data access
void interleavedLookup(Volume& target);

/// Tiles a volume recursively into bricks (using 3D Z ordering)
void tile(Volume16& target, const Volume16& source);

/// Copies a cropped version of the volume
void crop(Volume16& target, const Volume16& source, uint x1, uint y1, uint z1, uint x2, uint y2, uint z2);

/// Downsamples a volume by averaging 2x2x2 samples
void downsample(Volume16& target, const Volume16& source);

/// Converts volume data to ASCII (one voxel per line, explicit coordinates)
void toASCII(Volume& target, const Volume16& source);

/// Clips volume data to a cylinder and sets zero samples to 1
void clip(Volume16& target);

/// Returns an image of a volume slice
Image slice(const Volume& volume, uint z);

/// Returns the square root of an image of a volume slice
Image squareRoot(const Volume& volume, uint z);

