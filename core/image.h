#pragma once
/// \file image.h Image container and operations
#include "vector.h"

/// 2D array of pixels
generic struct ImageT : buffer<T> {
	union { int2 size = 0; struct { uint width, height; }; };
    uint stride = 0;
    bool alpha = false;

    ImageT() {}
    default_move(ImageT);
    ImageT(buffer<T>&& pixels, int2 size, uint stride=0, bool alpha=false) : buffer<T>(::move(pixels)), size(size), stride(stride?:size.x), alpha(alpha) {}
    ImageT(uint width, uint height, uint stride=0, bool alpha=false) : buffer<T>(height*stride), width(width), height(height), stride(stride?:width), alpha(alpha) {}
    ImageT(int2 size, uint stride=0, bool alpha=false) : ImageT(size.x, size.y, stride?:size.x, alpha) {}

	explicit operator bool() const { return buffer<T>::data && width && height; }
    inline T& operator()(uint x, uint y) const { assert(x<width && y<height, (int)x, (int)y, size); return buffer<T>::at(y*stride+x); }
    inline mref<T> row(uint y) const { assert(y<height); return buffer<T>::slice(y*stride, width); }
};
generic String str(const ImageT<T>& o) { return strx(o.size); }

generic void copy(const ImageT<T>& target, const ImageT<T>& o) {
 assert_(target.size == o.size);
 if(target.stride == o.stride) return target.copy(o);
 for(size_t y: range(o.height)) target.slice(y*target.stride, target.width).copy(o.slice(y*o.stride, o.width));
}
generic ImageT<T> copy(const ImageT<T>& o) { ImageT<T> target(o.size, o.stride, o.alpha); copy(target, o); return target; }

/// Returns a weak reference to \a image (unsafe if referenced image is freed)
generic ImageT<T> unsafeRef(const ImageT<T>& o) { return ImageT<T>(unsafeRef((const buffer<T>&)o),o.size,o.stride,o.alpha); }

/// Returns a weak reference to \a image (unsafe if referenced image is freed)
generic ImageT<T> cropRef(const ImageT<T>& o, int2 offset, int2 size) {
    assert_(offset+size <= o.size, offset, size, o.size);
    return ImageT<T>(unsafeRef(o.slice((uint64)offset.y*o.stride+offset.x, (uint64)size.y*o.stride-offset.x)),size,o.stride,o.alpha);
}

/// 2D array of 8bit integer pixels
typedef ImageT<uint8> Image8;
/// 2D array of 16bit integer samples
typedef ImageT<int16> Image16;
/// 2D array of 32bit integer samples
typedef ImageT<int32> Image32;
/// 2D array of BGRA 8-bit unsigned integer pixels (sRGB colorspace)
typedef ImageT<byte4> Image;
/// 2D array of 32bit floating-point pixels
typedef ImageT<float> ImageF;

// -- Resample (3x8bit) --

void box(const Image& target, const Image& source);
inline Image box(Image&& target, const Image& source) { box(target, source); return move(target); }
void bilinear(const Image& target, const Image& source);
/// Resizes \a source into \a target
void resize(const Image& target, const Image& source);
inline Image resize(Image&& target, const Image& source) { resize(target, source); return move(target); }
inline Image resize(int2 size, const Image& source) { return resize(Image(size, source.stride, source.alpha), source); }
