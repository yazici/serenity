/// \file family.cc Operations on families (pores)
#include "volume-operation.h"
#include "thread.h"
#include "display.h"
#include "family.h"

/// Parses a 3D position tuple
int3 parsePosition(TextData& s) {
    uint x=s.integer(); s.whileAny(", "_);
    uint y=s.integer(); s.whileAny(", "_);
    uint z=s.integer(); s.whileAny(", "_);
    return int3(x,y,z);
}

/// Converts text file formatted as ((x y z):( x y z)+)*\n to families
array<Family> parseFamilies(const string& data) {
    array<Family> families;
    TextData s(data);
    while(s) {
        s.skip("("_); Family family (zOrder(parsePosition(s))); s.skip("):\n"_); s.skip();
        while(s && s.peek()!='(') { family << zOrder(parsePosition(s)); s.skip(); }
        families << move(family);
    }
    return families;
}

void rootIndex(Volume16& target, const ref<Family>& families) {
    uint16* const targetData = target;
    clear(targetData,target.size());
    target.maximum = families.size; // 0 = background
    //target.maximum=0; for(const Family& family: families) { //if(!family.size) continue; target.maximum++; }
    target.maximum++; // maximum = Multiple family index
    uint i=0; for(const Family& family: families) {
        //if(!family.size) continue;
        i++;
        assert(i<1<<16);
        if(!targetData[family.root]) targetData[family.root] = i;
        else targetData[family.root] = target.maximum; // Multiple family
        for(uint64 index: family) {
            if(!targetData[index]) targetData[index] = i;
            else targetData[index] = target.maximum; // Multiple family
        }
    }
}
/// Writes root index of each voxel
class(RootIndex, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(uint16); }
    virtual void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>&, const ref<Result*>&, const ref<const Result*>& otherInputs) override {
        rootIndex(outputs[0],parseFamilies(otherInputs[0]->data));
    }
};

void colorizeIndex(Volume24& target, const Volume16& source) {
    bgr* const targetData = target;
    clear(targetData,target.size(),{0,0,0});
    bgr colors[target.maximum+1];
    for(uint i: range(target.maximum)) {
        vec3 rgb = HSVtoRGB(float((i*(target.maximum-1)/3)%target.maximum)/float(target.maximum), 1, 1);
        colors[i] = bgr{sRGB(rgb.x),sRGB(rgb.y),sRGB(rgb.z)};
    }
    colors[0]={0,0,0};
    colors[target.maximum]={0xFF,0xFF,0xFF};
    chunk_parallel(source.size(), [&](uint, uint offset, uint size) {
        const uint16* const sourceData = source + offset;
        bgr* const targetData = target + offset;
        for(uint i : range(size)) targetData[i] = colors[sourceData[i]];
    });
    target.maximum = 0xFF;
}
/// Colorizes each index in a different color
class(ColorizeIndex, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(bgr); }
    virtual void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        colorizeIndex(outputs[0], inputs[0]);
    }
};

/*array<mat4> bound(const ref<Family>& families) {
    array<mat4> bounds (families.size);
    uint i=0; for(const Family& family: families) {
        //if(!family.size) continue;
        i++;
        assert(i<1<<16);
        if(!targetData[family.root]) targetData[family.root] = i;
        else targetData[family.root] = target.maximum; // Multiple family
        for(uint64 index: family) {
            if(!targetData[index]) targetData[index] = i;
            else targetData[index] = target.maximum; // Multiple family
        }
    }
}
/// Writes root index of each voxel
class(Bound, Operation) {
    virtual void execute(const Dict&, const mref<Result*>& outputs, const ref<Result*>& inputs) override {
        outputs[0] = rootIndex(parseFamilies(otherInputs[0]->data));
    }
};
*/
