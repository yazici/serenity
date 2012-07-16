#pragma once
#include "stream.h"
#include "vector.h"
#include "image.h"
#include "file.h"
#include "map.h"

struct Glyph {
    int2 offset; // (left bearing, min.y-baseline) (in .4)
    int advance; //in .4
    Image<uint8> image;
};

/// Truetype font renderer stub
struct Font {
    Map keep;
    DataStream cmap;
    uint16* hmtx;
    void* loca; uint16 indexToLocFormat, numOfLongHorMetrics;
    byte* glyf; uint scale, round, size;
    Glyph cache[256]; //TODO: Unicode

    /// Opens font at /a path scaled to /a size pixels high
    Font(const ref<byte>& path, int size);
    /// Returns kerning space between \a left and \a right
    int kerning(uint16 left, uint16 right); //space in .4
    /// Caches and returns glyph for \a code
    Glyph glyph(uint16 code);
private:
    uint16 index(uint16 code);
};
