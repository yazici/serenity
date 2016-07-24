#pragma once
#include "data.h"
#include "image.h"

/// 2D array of 16bit integer samples
typedef ImageT<int16> Image16;

struct Section { size_t start, size; String name; };
inline bool operator>(const Section& a, const Section& b) { return a.start > b.start; }

struct CR2 {
 bool onlyParse = false;
 array<Section> sections;
 array<uint*> zeroOffset; // Words to zero to delete JPEG thumb (but keep EXIF) (1MB)
 array<uint*> ifdOffset; // Replace nextIFD after JPEG thumb with 3rd IFD (RAW) to remove RGB thumb (1MB)
 struct Entry { uint16 tag, type; uint count; uint value; };
 array<Entry*> entriesToFix; // Entries which would have dangling references after truncation
 ref<byte> data;
 int2 size = 0; size_t stride = 0;
 const uint8* pointer = 0;
 uint bitbuf = 0;
 int vbits = 0;
 uint readBits(const int nbits);

 struct LengthSymbol { uint8 length = 0; uint8 symbol = 0; };
 int maxLength[2] = {0,0};
 buffer<LengthSymbol> lengthSymbolForCode[2] = {};
 int readHuffman(uint i);

 struct { uint16 R, G, B; } whiteBalance = {0,0,0};
 Image16 image;

 void readIFD(BinaryData& s);
 CR2(const ref<byte> file, bool onlyParse=false);
};
