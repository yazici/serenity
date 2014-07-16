#include "volume-operation.h"
#include "thread.h"
#include "time.h"

/// Denoises a volume using a cubic median filter
template<int size, Type T> void median(VolumeT<T>& target, const VolumeT<T>& source) {
    assert_(source.sampleCount-2*source.margin>int3(size+1+size), "Input too small for ",size+1+size,"³ median filter", source.sampleCount-2*source.margin);
    assert_(!source.tiled());
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    const int marginX=source.margin.x+size, marginY=source.margin.y+size, marginZ=source.margin.z+size;
    target.margin = int3(marginX, marginY, marginZ);
    const T* const sourceData = source;
    T* const targetData = target;
    uint maximum[8] = {};
    Time time; Time report;
    parallel(marginZ, Z-marginZ, [&](uint id, uint z) {
        if(id==0 && report/1000>=8) log(z,"/", Z-2*marginZ, (z*X*Y/1024./1024.)/(time/1000.), "MS/s"), report.reset();
        for(uint y: range(marginY, Y-marginY)) {
            uint8 histogram4[1<<4] = {};
            uint8 histogram8[1<<8] = {};
            uint8 histogram12[1<<12] = {}; //4K
            uint8 histogram16[1<<16] = {}; //64K
            const T* const sourceZX = sourceData + z*X*Y + y*X;
            for(int dz=-size; dz<=size; dz++) for(int dy=-size; dy<=size; dy++) for(int x=marginX-size; x<marginX+size; x++) { // Initializes histograms
                T value = sourceZX[dz*X*Y+dy*X+x];
                histogram4[value>>12]++, histogram8[value>>8]++, histogram12[value>>4]++, histogram16[value]++;
            }
            for(int x: range(marginX, X-marginX)) { //TODO: vector
                for(int dz=-size; dz<=size; dz++) for(int dy=-size; dy<=size; dy++) { // Updates histogram with values entering the window
                    T value = sourceZX[dz*X*Y+dy*X+x+1];
                    histogram4[value>>12]++, histogram8[value>>8]++, histogram12[value>>4]++, histogram16[value]++;
                }
                uint count=0;
                uint i=0; for(; i<16; i++) { uint after = count + histogram4[i]; if(after > 27/2) break; count = after; } uint offset = i<<4;
                uint j=0; for(; j<16; j++) { uint after = count + histogram8[offset+j]; if(after > 27/2) break; count = after; } offset = (offset+j)<<4;
                uint k=0; for(; k<16; k++) { uint after = count + histogram12[offset+k]; if(after > 27/2) break; count = after; } offset = (offset+k)<<4;
                for(uint l=0; l<16; l++) {
                    count += histogram16[offset+l];
                    if(count > 27/2) {
                        uint value = (i<<12) + (j<<8) + (k<<4) + l;
                        targetData[z*X*Y+y*X+x] = value;
                        if(value > maximum[id]) maximum[id] = value;
                        break;
                    }
                }
                for(int dz=-size; dz<=size; dz++) for(int dy=-size; dy<=size; dy++) { // Updates histogram with values leaving the window
                    T value = sourceZX[dz*X*Y+dy*X+x-1];
                    histogram4[value>>12]--, histogram8[value>>8]--, histogram12[value>>4]--, histogram16[value]--;
                }
            }
        }
    });
    target.maximum = max(ref<uint>(maximum));
}

struct Median : VolumeOperation {
    uint outputSampleSize(const Dict&, const ref<const Result*>& inputs, uint) override { return toVolume(*inputs[0]).sampleSize; }
    string parameters() const override { return "radius"_; }
    void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        int radius = args.value("radius"_,1);
        if(radius==1) {
            /**/  if(inputs[0].sampleSize==sizeof(uint8)) median<1, uint8>(outputs[0], inputs[0]); // Median in a 3³ window (27 samples)
            else if(inputs[0].sampleSize==sizeof(uint16)) median<1, uint16>(outputs[0], inputs[0]); // Median in a 3³ window (27 samples)
        }
        else error("Unsupported radius",radius);
    }
};
template struct Interface<Operation>::Factory<Median>;