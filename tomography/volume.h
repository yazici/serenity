#pragma once
#include "image.h"
#include "file.h"

struct VolumeF {
    VolumeF(int3 size) : size(size), data(size.z*size.y*size.x) {}
    VolumeF(int3 size, const ref<float>& data) : size(size), data(data) { assert_(data.size == (size_t)size.x*size.y*size.z); }
    VolumeF(const ref<float>& data) : VolumeF(round(pow(data.size,1./3)), data) {}
    VolumeF(Map&& map) : VolumeF((ref<float>)map) { this->map = move(map); }
    inline float& operator()(uint x, uint y, uint z) const {assert_(x<uint(size.x) && y<uint(size.y) && z<uint(size.z), x,y,z, size); return data[(size_t)z*size.y*size.x+y*size.x+x]; }

    int3 size = 0;
    buffer<float> data;
    Map map;
};

inline ImageF slice(const VolumeF& volume, size_t index /* Z slice or projection*/) {
    int3 size = volume.size;
    assert_(index < size_t(size.z), index);
    return ImageF(buffer<float>(volume.data.slice(index*size.y*size.x,size.y*size.x)), int2(size.x,size.y));
}

inline float sum(const VolumeF& volume) { return sum(volume.data); }