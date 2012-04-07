#include "flac.h"
#include "process.h"

/// Returns the number of cycles used to execute \a statements
inline uint64 rdtsc() {
    asm volatile("xorl %%eax,%%eax \n cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx"); //serialize
    uint32 lo, hi; asm volatile("rdtsc" : "=a" (lo), "=d" (hi)); return (uint64)hi << 32 | lo; }
#define cycles( statements ) ({ uint64 start=rdtsc(); statements; rdtsc()-start; })
struct tsc { uint64 start=rdtsc(); operator uint64(){ return rdtsc()-start; } };

#define swap64 __builtin_bswap64

void BitReader::setData(array<byte>&& buffer) { array<byte>::operator=(move(buffer)); data=(ubyte*)array::data(); bsize=8*array::size(); index=0; }

void BitReader::skip(int count) { index+=count; }

uint BitReader::bit() { uint8 bit = uint8(data[index/8]<<(index&7))>>7; index++; return bit; }

uint BitReader::binary(int size) {
    assert(size<=32);
    assert(index<bsize);
    uint64 word = swap64(*(uint64*)(data+index/8)) << (index&7);
    index += size;
    return word>>int8(64-size);
}

int BitReader::sbinary(int size) {
    assert(size<=32);
    assert(index<bsize);
    int64 word = swap64(*(uint64*)(data+index/8)) << (index&7);
    index += size;
    return word>>int8(64-size);
}

static uint8 log2[256];
declare(static void generate_log2(), constructor) { int i=1; for(int l=0;l<=7;l++) for(int r=0;r<1<<l;r++) log2[i++]=7-l; assert(i==256); }
uint BitReader::unary() {
    uint64 w = swap64(*(uint64*)(data+index/8)) << (index&7);
    assert(w);
    uint8 b=w>>(64-8);
    uint size=0;
    while(!b) size+=8, w<<=8, b=w>>(64-8);
    size += log2[b];
    index += size+1;
    return size;
}

/// Reads an UCS-2 encoded value
uint BitReader::utf8() {
    uint code = binary(8);
    if(code&0b10000000) {
        int test = code;
        code &= 0b00011111; code <<= 6; code |= binary(8)&0b111111;
        if(test&0b00100000) {
            int test = code;
            code &= 0b00001111111111; code <<= 6; code |= binary(8)&0b111111;
            if(test&0b10000000000) {
                assert(!(code&0b1000000000000000));
                code &= 0b00000111111111111111; code <<= 6; code |= binary(8)&0b111111;
            }
        }
    }
    return code;
}

void FLAC::open(array<byte>&& data) {
    BitReader::setData(move(data));
    skip(32); //fLaC
    for(;;) { //METADATA_BLOCK*
        bool last=bit();
        enum BlockType { STREAMINFO, PADDING, APPLICATION, SEEKTABLE, VORBIS_COMMENT, CUESHEET, PICTURE };
        uint blockType = binary(7); assert(blockType<=PICTURE, bin(blockType));
        int size = binary(24);
        if(blockType==STREAMINFO) {
            assert(size=0x22);
            skip(16+16+24+24);
            int unused sampleRate = binary(20); assert(sampleRate==48000);
            int unused channels = binary(3)+1; assert(channels==2);
            int unused bitsPerSample = binary(5)+1; assert(bitsPerSample==24);
            time = (binary(36-24)<<24) | binary(24); //time = binary(36);
            skip(128); //MD5
        } else skip(size*8);
        if(last) break;
    };
}

template<int unroll> inline void unroll_predictor(uint order, double* predictor, double* context, double* odd, int* out, int* end, int shift) {
    assert(order<=2*unroll,order,unroll);
    //ensure enough warmup before using unrolled version
    for(uint i=order;i<2*unroll;i++) { //actually executed once (only for odd order) if specializing for every even order
        double sum=0;
        for(uint i=0;i<order;i++) sum += predictor[i] * context[i]; //"slow" loop
        int sample = (int64(sum)>>shift) + *out; //add residual to prediction
        context[i]=odd[i]= (double)sample; //write out context
        *out = sample; out++; //write out decoded sample
    }
    for(int i=order-1;i>=0;i--) predictor[2*unroll-order+i]=predictor[i]; //move predictors to right place for unrolled loop
    for(uint i=0;i<2*unroll-order;i++) predictor[i]=0; //clear extra predictors used because of unrolling
    double2 kernel[unroll];
    for(uint i=0;i<unroll;i++) kernel[i] = *(double2*)&predictor[2*i]; //load predictor in registers
    for(;out<end;) {
#define ALIGN 1
#if ALIGN
        {//filter using aligned context for even samples
            double2 sum = {0,0};
            for(uint i=0;i<unroll;i++) sum += kernel[i] * *(double2*)(context+2*i); //unrolled loop (for even samples)
            int sample = (int64(extract_d(sum,0)+extract_d(sum,1))>>shift) + *out; //add residual to prediction
            context[2*unroll]= odd[2*unroll]= (double)sample; context++; odd++; //write out context (misalign context, align odd)
            *out = sample; out++; //write out decoded sample}
        }

        {//filter using aligned context for odd samples
            double2 sum = {0,0};
            for(uint i=0;i<unroll;i++) sum += kernel[i] * *(double2*)(odd+2*i); //unrolled loop (for odd samples)
            int sample = (int64(extract_d(sum,0)+extract_d(sum,1))>>shift) + *out; //add residual to prediction
            context[2*unroll]=odd[2*unroll]= (double)sample; context++; odd++; //write out context (align context, misalign odd)
            *out = sample; out++; //write out decoded sample
        }
#else
        {//filter using unaligned context
            double2 sum = {0,0};
            for(uint i=0;i<unroll;i++) sum += kernel[i] * loadu_pd(context+2*i); //unrolled loop (for even samples)
            int sample = (int64(extract_d(sum,0)+extract_d(sum,1))>>shift) + *out; //add residual to prediction
            context[2*unroll]= (double)sample; context++; odd++; //write out context (misalign context, align odd)
            *out = sample; out++; //write out decoded sample}
        }
#endif
    }
}

uint64 rice=0, predict=0, order=0;
void FLAC::readFrame() {
    position=0;
    //Frame
    int unused sync = binary(15); assert(sync==0b111111111111100,bin(sync));
    bool unused variable = bit();
    int blockSize_[16] = {0, 192, 576,1152,2304,4608, -8,-16, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
    blockSize = blockSize_[binary(4)];
    int sampleRate_[16] = {0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000, -1, -2, -20 };
    int unused sampleRate = sampleRate_[binary(4)]; assert(sampleRate==48000);
    enum { Independent=1, LeftSide=8, RightSide=9, MidSide=10 };
    int channels = binary(4); assert(channels==Independent||channels==LeftSide||channels==MidSide||channels==RightSide,channels);
    int sampleSize_[8] = {0, 8, 12, 0, 16, 20, 24, 0};
    int sampleSize = sampleSize_[binary(3)]; assert(sampleSize==24);
    int unused zero = bit(); assert(zero==0);
    int unused frameNumber = utf8();
    if(blockSize<0) blockSize = binary(-blockSize)+1;
    assert(blockSize>0&&uint(blockSize)<=sizeof(buffer)/sizeof(int2),blockSize);
    skip(8);

    int block[2][blockSize];
    for(int channel=0;channel<2;channel++) {
        int* out = block[channel];
        int* end = out;
        double buffer1[blockSize]; double* context = buffer1;
        double buffer2[blockSize+1]; double* odd = buffer2+1;
        if(ptr(context)%16) odd=buffer1, context=buffer2+1;
        assert(ptr(context)%16==0 && ptr(odd)%16==8,hex(int64(context)%16),hex(int64(odd)%16)); //copy of context aligned so as to filter odd samples

        int rawSampleSize = sampleSize;
        if(channel == 0) { if(channels==RightSide) rawSampleSize++; }
        else if(channels==LeftSide || channels == MidSide) rawSampleSize++;

        //Subframe
        int unused zero = bit(); assert(zero==0);
        int type = binary(6);
        int unused wasted = bit(); assert(wasted==0);
        if (type == 0) { //constant
            int constant = binary(rawSampleSize);
            for(int i = 0;i<blockSize;i++) *out++ = constant;
            continue;
        }

        uint i=0;
        uint order;
        int shift;
        double predictor[32];

        if (type >= 32) { //LPC
            order = (type&~0x20)+1; assert(order>0 && order<=32,order);
            for(;i<order;i++) context[i]=odd[i]= *out++ = sbinary(rawSampleSize);
            int precision = binary(4)+1; assert(precision==15,precision);
            shift = sbinary(5); assert(shift>=0);
            for(uint i=0;i<order;i++) predictor[order-1-i]= sbinary(precision);
        } else if(type>=8 && type <=12) { //Fixed
            order = type & ~0x8; assert(order>0 && order<=4);
            for(;i<order;i++) context[i]=odd[i]= *out++ = sbinary(rawSampleSize);
            if(order==1) predictor[0]=1;
            else if(order==2) predictor[0]=-1, predictor[1]=2;
            else if(order==3) predictor[0]=1, predictor[1]=-3, predictor[2]=3;
            else if(order==4) predictor[0]=-1, predictor[1]=4, predictor[2]=-6, predictor[3]=4;
            shift=0;
        } else fail();

        //Residual
        int method = binary(2); assert(method<=1,method);
        int parameterSize = method == 0 ? 4 : 5;
        int escapeCode = (1<<parameterSize)-1;
        int partitionOrder = binary(4);
        uint size = blockSize >> partitionOrder;
        int partitionCount = 1<<partitionOrder;

        tsc rice;
        for(int p=0;p<partitionCount;p++) {
            end += size;
            int k = binary( parameterSize );
            // 5 registers [out, end, k, data, index]
            if(k==0) {
                for(;out<end;) {
                    uint u = unary() << k;
                    *out++ = (u >> 1) ^ (-(u & 1));
                }
            } else if(k < escapeCode) { //TODO: static dispatch on k?
                //disasm(
                for(;out<end;) {
                    uint u = unary() << k;
                    u |= binary(k);
                    *out++ = (u >> 1) ^ (-(u & 1));
                }
                //)
            } else {
                int n = binary(5); assert(n<=16,n);
                for(;out<end;) {
                    int u = binary(n);
                    *out++ = (u >> 1) ^ (-(u & 1));
                }
            }
        }
        ::rice += rice;

        tsc predict;
        out=block[channel]+order;
#define UNROLL 1
#if UNROLL
#define o(n) case n: unroll_predictor<n>(order,predictor,context,odd,out,end,shift); break;
        switch((order+1)/2) {
         o(1)o(2)o(3)o(4)o(5)o(6)o(7)o(8)o(9)o(10)o(11)o(12)o(13)o(14) //keep predictor in 14 SSE registers until order=28
         o(15)o(16) //order>28 will spill the predictor out of registers
        }
#else
        for(;out<end;) {
            /*double sum=0;
            for(uint i=0;i<order;i++) sum += (int64)predictor[i] * context[i];
            int residual = (int64(sum)>>shift) + *out;*/
            double sum=0;
            for(uint i=0;i<order;i++) sum += predictor[i] * context[i];
            int sample = (int64(sum)>>shift) + *out; //add residual to prediction
            context[order] = (double)sample; context++;  //write out context
            *out = sample; out++; //write out decoded sample
        }
#endif
        ::predict += predict;
        ::order += order*blockSize;
    }
    index=align(8,index);
    skip(16);
    for(int i=0;i<blockSize;i++) {
        int a=block[0][i], b=block[1][i];
        if(channels==Independent) buffer[i] = int2(a,b);
        if(channels==LeftSide) buffer[i] = int2(a,a-b);
        if(channels==MidSide) a-=b>>1, buffer[i] = int2(a + b, a);
        if(channels==RightSide) buffer[i] = int2(a+b,b);
    }
}
