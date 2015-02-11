#include "core/image.h"
#include "data.h"
#include "vector.h"
#include "parallel.h"
#include "math.h"
#include "map.h"

// -- sRGB --

uint8 sRGB_forward[0x1000];  // 4K (FIXME: interpolation of a smaller table might be faster)
void __attribute((constructor(1001))) generate_sRGB_forward() {
    for(uint index: range(sizeof(sRGB_forward))) {
	real linear = (real) index / (sizeof(sRGB_forward)-1);
	real sRGB = linear > 0.0031308 ? 1.055*pow(linear,1/2.4)-0.055 : 12.92*linear;
	assert(abs(linear-(sRGB > 0.04045 ? pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92))<exp2(-50));
	sRGB_forward[index] = round(0xFF*sRGB);
    }
}

float sRGB_reverse[0x100];
void __attribute((constructor(1001))) generate_sRGB_reverse() {
    for(uint index: range(0x100)) {
	real sRGB = (real) index / 0xFF;
	real linear = sRGB > 0.04045 ? pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92;
	assert(abs(sRGB-(linear > 0.0031308 ? 1.055*pow(linear,1/2.4)-0.055 : 12.92*linear))<exp2(-50));
	sRGB_reverse[index] = linear;
	assert(sRGB_forward[int(round(0xFFF*sRGB_reverse[index]))]==index);
    }
}

// -- Decode --

string imageFileFormat(const ref<byte> file) {
    if(startsWith(file,"\xFF\xD8")) return "JPEG"_;
    else if(startsWith(file,"\x89PNG\r\n\x1A\n")) return "PNG"_;
    else if(startsWith(file,"\x00\x00\x01\x00")) return "ICO"_;
    else if(startsWith(file,"\x49\x49\x2A\x00") || startsWith(file,"\x4D\x4D\x00\x2A")) return "TIFF"_;
    else if(startsWith(file,"BM")) return "BMP"_;
    else return ""_;
}

int2 imageSize(const ref<byte> file) {
    BinaryData s(file, true);
	// PNG
    if(s.match(ref<uint8>{0b10001001,'P','N','G','\r','\n',0x1A,'\n'})) {
        for(;;) {
            s.advance(4); // Length
            if(s.read<byte>(4) == "IHDR"_) {
                uint width = s.read(), height = s.read();
                return int2(width, height);
            }
        }
		error("PNG");
    }
	// JPEG
    enum Marker : uint8 {
        StartOfFrame = 0xC0, DefineHuffmanTable = 0xC4, StartOfImage = 0xD8, EndOfImage = 0xD9,
        StartOfSlice = 0xDA, DefineQuantizationTable = 0xDB, DefineRestartInterval = 0xDD, ApplicationSpecific = 0xE0 };
    if(s.match(ref<uint8>{0xFF, StartOfImage})) {
        for(;;){
            s.skip((uint8)0xFF);
            uint8 marker = s.read();
            if(marker == EndOfImage) break;
            if(marker==StartOfSlice) {
                while(s.available(2) && ((uint8)s.peek() != 0xFF || uint8(s.peek(2)[1])<0xC0)) s.advance(1);
            } else {
                uint16 length = s.read(); // Length
				if(marker>=StartOfFrame && marker<=StartOfFrame+2) {
                    uint8 precision = s.read(); assert_(precision==8);
                    uint16 height = s.read();
                    uint16 width = s.read();
                    return int2(width, height);
                    //uint8 components = s.read();
                    //for(components) { ident:8, h_samp:4, v_samp:4, quant:8 }
				} else s.advance(length-2);
            }
        }
		error("JPG");
    }
    error("Unknown image format", hex(file.size<16?file:s.peek(16)));
}

Image  __attribute((weak)) decodePNG(const ref<byte>) { error("PNG support not linked"); }
Image  __attribute((weak)) decodeJPEG(const ref<byte>) { error("JPEG support not linked"); }
Image  __attribute((weak)) decodeICO(const ref<byte>) { error("ICO support not linked"); }
Image  __attribute((weak)) decodeTIFF(const ref<byte>) { error("TIFF support not linked"); }
Image  __attribute((weak)) decodeBMP(const ref<byte>) { error("BMP support not linked"); }
Image  __attribute((weak)) decodeTGA(const ref<byte>) { error("TGA support not linked"); }

Image decodeImage(const ref<byte> file) {
    if(startsWith(file,"\xFF\xD8")) return decodeJPEG(file);
    else if(startsWith(file,"\x89PNG")) return decodePNG(file);
    else if(startsWith(file,"\x00\x00\x01\x00")) return decodeICO(file);
    else if(startsWith(file,"\x00\x00\x02\x00")||startsWith(file,"\x00\x00\x0A\x00")) return decodeTGA(file);
    else if(startsWith(file,"\x49\x49\x2A\x00") || startsWith(file,"\x4D\x4D\x00\x2A")) return decodeTIFF(file);
    else if(startsWith(file,"BM")) return decodeBMP(file);
	else error("Unknown image format", hex(file.slice(0,min<int>(file.size,4))));
}

// -- Rotate --

void rotate(const Image& target, const Image& source) {
    assert_(target.size.x == source.size.y && target.size.y == source.size.x, source.size, target.size);
    for(int y: range(source.height)) for(int x: range(source.width)) target(source.height-1-y, x) = source(x,y);
}

Image rotateHalfTurn(Image&& target) {
    for(size_t y: range(target.height)) for(size_t x: range(target.width/2)) swap(target(x,y), target(target.width-1-x, y)); // Reverse rows
    for(size_t y: range(target.height/2)) for(size_t x: range(target.width)) swap(target(x,y), target(x, target.height-1-y)); // Reverse columns
    return move(target);
}

// -- Resample (3x8bit) --

static void box(const Image& target, const Image& source) {
    //assert_(!source.alpha); //FIXME: not alpha correct
    //assert_(source.size.x/target.size.x == source.size.y/target.size.y, target, source, source.size.x/target.size.x, source.size.y/target.size.y);
    int scale = min(source.size.x/target.size.x, source.size.y/target.size.y);
    assert_(scale <= 512, target.size, source.size);
    assert_((target.size-int2(1))*scale+int2(scale-1) < source.size, target, source);
    chunk_parallel(target.height, [&](uint, size_t y) {
	const byte4* sourceLine = source.data + y * scale * source.stride;
	byte4* targetLine = target.begin() + y * target.stride;
	for(uint unused x: range(target.width)) {
	    const byte4* sourceSpanOrigin = sourceLine + x * scale;
	    uint4 sum = 0;
	    for(uint i: range(scale)) {
		const byte4* sourceSpan = sourceSpanOrigin + i * source.stride;
		for(uint j: range(scale)) {
		    uint4 s (sourceSpan[j]);
		    s.b = s.b*s.a; s.g = s.g*s.a; s.r = s.r*s.a;
		    sum += uint4(s);
		}
	    }
	    if(sum.a) { sum.b = sum.b / sum.a; sum.g = sum.g / sum.a; sum.r = sum.r / sum.a; }
	    sum.a /= scale*scale;
	    targetLine[x] = byte4(sum[0], sum[1], sum[2], sum[3]);
	}
    });
}
static Image box(Image&& target, const Image& source) { box(target, source); return move(target); }

static void bilinear(const Image& target, const Image& source) {
    //assert_(!source.alpha, source.size, target.size);
    const uint stride = source.stride;
    chunk_parallel(target.height, [&](uint, size_t y) {
	for(uint x: range(target.width)) {
	    const uint fx = x*256*(source.width-1)/target.width, fy = y*256*(source.height-1)/target.height; //TODO: incremental
	    uint ix = fx/256, iy = fy/256;
	    uint u = fx%256, v = fy%256;
	    const ref<byte4> span = source.slice(iy*stride+ix);
	    byte4 d = 0;
	    uint a  = ((uint(span[      0][3]) * (256-u) + uint(span[           1][3])  * u) * (256-v)
		    + (uint(span[stride][3]) * (256-u) + uint(span[stride+1][3]) * u) * (       v) ) / (256*256);
	    if(a) for(int i=0; i<3; i++) { // Interpolates values as if in linear space (not sRGB)
		d[i] = ((uint(span[      0][3]) * uint(span[      0][i]) * (256-u) + uint(span[           1][3]) * uint(span[           1][i]) * u) * (256-v)
			+ (uint(span[stride][3]) * uint(span[stride][i]) * (256-u) + uint(span[stride+1][3]) * uint(span[stride+1][i]) * u) * (       v) )
			/ (a*256*256);
	    }
	    d[3] = a;
	    target(x, y) = d;
	}
    });
}

void resize(const Image& target, const Image& source) {
    assert_(source && target && source.size != target.size, source, target);
    if(source.width%target.width==0 && source.height%target.height==0) box(target, source); // Integer box downsample
    else if(target.size > source.size/2) bilinear(target, source); // Bilinear resample
    else { // Integer box downsample + Bilinear resample
        int downsampleFactor = min(source.size.x/target.size.x, source.size.y/target.size.y);
	assert_(downsampleFactor, target, source);
	bilinear(target, box(Image((source.size/*+int2((downsampleFactor-1)/2)*/)/downsampleFactor, source.alpha), source));
    }
}

#if 0
// -- 4x float

ImageF convert(const Image& source) {
    ImageF target (source.size, source.alpha);
    parallel_chunk(source.Ref::size, [&](uint, size_t I0, size_t DI) {
	extern float sRGB_reverse[0x100];
	for(size_t i: range(I0, I0+DI)) target[i] = {sRGB_reverse[source[i][0]], sRGB_reverse[source[i][1]], sRGB_reverse[source[i][2]],
						     source.alpha ? float(source[i][3])/0xFF : 1};
    });
    return target;
}

Image convert(const ImageF& source) {
	Image target (source.size);
	assert(source.Ref::size == target.Ref::size);
	parallel_chunk(source.Ref::size, [&](uint, size_t I0, size_t DI) {
		extern uint8 sRGB_forward[0x1000];
		int clip = 0;
		for(size_t i: range(I0, I0+DI)) {
			int3 linear;
			for(uint c: range(3)) {
				float v = source[i][c];
				if(!(v >= 0 && v <= 1)) {
					if(v < 0) v = 0;
					else if(v > 1) v = 1;
					else v = 0; // NaN
					//if(!clip) log("Clip", v, i, c);
					clip++;
				}
				linear[c] = int(round(0xFFF*v));
			}
			target[i] = byte4( sRGB_forward[linear[0]], sRGB_forward[linear[1]], sRGB_forward[linear[2]] );
		}
		//if(clip) log("Clip", clip);
		//assert(!clip);
	});
	return target;
}

// Box convolution
void box(const ImageF& target, const ImageF& source, const int width) {
	assert_(target.size.y == source.size.x && target.size.x == source.size.y && uint(target.stride) == target.width && uint(source.stride)==source.width);
	parallel_chunk(source.size.y, [&](uint, int Y0, int DY) { // Top
		const v4sf* const sourceData = source.data;
		v4sf* const targetData = target.begin();
		const uint sourceStride = source.stride;
		const uint targetStride = target.stride;
		for(int y: range(Y0, Y0+DY)) {
			const v4sf* const sourceRow = sourceData + y * sourceStride;
			v4sf* const targetColumn = targetData + y;
			v4sf sum = float4(0);
			for(uint x: range(width)) sum += sourceRow[x];
			float N = width;
			for(uint x: range(width)) {
				sum += sourceRow[x+width];
				N++;
				const v4sf scale = float4(1./N);
				targetColumn[x * targetStride] = scale * sum;
			}
			const v4sf scale = float4(1./N);
			for(uint x: range(width, sourceStride-width)) {
				v4sf const* source = sourceRow + x;
				sum += source[width];
				targetColumn[x * targetStride] = scale * sum;
				sum -= source[-width];
			}
			for(uint x: range(sourceStride-width, sourceStride)) {
				const v4sf scale = float4(1./N);
				targetColumn[x * targetStride] = scale * sum;
				sum -= sourceRow[x-width];
				N--;
			}
		}
	});
}
#endif
