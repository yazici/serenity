#pragma once
#include "operation.h"
#include "volume.h"

/// Returns floor(log2(v))
inline uint log2(uint v) { uint r=0; while(v >>= 1) r++; return r; }
/// Computes the next highest power of 2
inline uint nextPowerOfTwo(uint v) { v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; v++; return v; }
inline int3 nextPowerOfTwo(int3 v) { return int3(nextPowerOfTwo(v.x), nextPowerOfTwo(v.y), nextPowerOfTwo(v.z)); }

// Converts an abstract operation Result to a volume
inline Volume toVolume(const string& metadata, const buffer<byte>& data) {
    Volume volume;
    if( !parseVolumeFormat(volume, metadata) ) return Volume();
    volume.data = buffer<byte>(ref<byte>(data));
    volume.sampleSize = volume.data.size / volume.size();
    assert(volume.sampleSize >= align(8, nextPowerOfTwo(log2(nextPowerOfTwo((volume.maximum+1))))) / 8); // Minimum sample size to encode maximum value (in 2ⁿ bytes)
    assert_(volume.sampleCount>2*volume.margin, "Empty volume"_, volume.sampleCount-2*volume.margin);
    return volume;
}
inline Volume toVolume(const Result& result) { return toVolume(result.metadata, result.data); }

/// Convenience class to help define volume operations
struct VolumeOperation : Operation {
    /// Overriden by implementation to return required output sample size (or 0 for non-volume output)
    virtual uint outputSampleSize(uint index unused) { return 0; } // No volume output by default
    virtual uint outputSampleSize(const Dict&, const ref<const Result*>&, uint index) { return this->outputSampleSize(index); }
    size_t outputSize(const Dict& args, const ref<const Result*>& inputs, uint index) override {
        assert(inputs);
        assert(toVolume(*inputs[0]), inputs[0]->name, inputs[0]->metadata, inputs[0]->data.size);
        return toVolume(*inputs[0]).size() * this->outputSampleSize(args,inputs,index);
    }
    /// Actual operation (overriden by implementation)
    virtual void execute(const Dict& args unused, const mref<Volume>& outputs unused, const ref<Volume>& inputs unused) { error("None of the execute methods were overriden by implementation"_); }
    /// Actual operation (overriden by implementation) with additional non-volume outputs
    virtual void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>&) { this->execute(args, outputs, inputs); }
    /// Actual operation (overriden by implementation) with additional non-volume inputs
    virtual void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<const Result*>&) { this->execute(args, outputs, inputs); }
    /// Actual operation (overriden by implementation) with additional non-volume outputs and inputs
    virtual void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>& otherOutputs, const ref<const Result*>& otherInputs) {
        if(!otherOutputs && !otherInputs) return this->execute(args, outputs, inputs);
        if(otherOutputs && !otherInputs) return this->execute(args, outputs, inputs, otherOutputs);
        if(otherInputs && !otherOutputs) return this->execute(args, outputs, inputs, otherInputs);
        error("Implementation ignores all non-volume inputs and non-volume outputs", outputs, inputs, otherOutputs, otherInputs);
    }
    void execute(const Dict& args, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        array<Volume> inputVolumes; array<const Result*> otherInputs;
        for(const Result* input: inputs) {
            Volume volume = toVolume(*input);
            if(volume) inputVolumes << move(volume);
            else otherInputs << input;
        }
        array<Volume> outputVolumes; array<Result*> otherOutputs;
        for(uint index: range(outputs.size)) {
            uint sampleSize = this->outputSampleSize(args, inputs, index);
            if(sampleSize) {
                Volume volume;
                volume.sampleSize = sampleSize;
                assert(outputs[index]->data);
                volume.data = unsafeReference(outputs[index]->data);
                if(inputVolumes) { // Inherits initial metadata from previous operation
                    const Volume& source = inputVolumes.first();
                    volume.sampleCount=source.sampleCount; volume.copyMetadata(source);
                    assert(volume.sampleSize * volume.size() == volume.data.size);
                    if(source.tiled()) interleavedLookup(volume);
                }
                outputVolumes << move( volume );
            } else {
                otherOutputs << outputs[index];
            }
        }
        execute(args, outputVolumes, inputVolumes, otherOutputs, otherInputs);
        uint outputVolumesIndex=0; for(uint index: range(outputs.size)) {
            uint sampleSize = this->outputSampleSize(args, inputs, index);
            if(sampleSize) {
                Volume& output = outputVolumes[outputVolumesIndex++];
                assert_(output.sampleCount>2*output.margin, "Empty output volume"_, output.sampleCount-2*output.margin);
                outputs[index]->metadata = volumeFormat(output);
                outputs[index]->data.size = output.data.size;
            }
        }
    }
};

/// Convenience class to define a single input, single output volume operation
template<Type O> struct VolumePass : VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(O); }
    virtual void execute(const Dict& args, VolumeT<O>& target, const Volume& source) abstract;
    virtual void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs) override { execute(args, outputs[0], inputs[0]); }
};
#define defineVolumePass(name, type, function) \
    struct name : VolumePass<type> { \
     void execute(const Dict&, VolumeT<type>& target, const Volume& source) override { function(target, source); } \
    }; \
    template struct Interface<Operation>::Factory<name>
