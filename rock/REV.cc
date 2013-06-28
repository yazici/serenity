#include "process.h"
#include "volume-operation.h"
#include "sample.h"

/// Returns relative deviation versus cylinder radius of 8 volume samples
class(REV, Tool) {
    void execute(const Dict& arguments, const ref<Result*>& outputs, const ref<Result*>&, ResultManager& results) override {
        Volume input = toVolume(results.getResult("connected"_, arguments));
        int margin = max(max(input.margin.x, input.margin.y), input.margin.z), size=min(min(input.sampleCount.x, input.sampleCount.y), input.sampleCount.z);
        map<String, buffer<byte>> PSD_R, PSD_octant;
        NonUniformSample relativeDeviations[3];
        const real ratio = (real)(margin+2)/(margin+1);
        for(double r=margin+1; round(r)<(size-margin)/4; r*=ratio) {
            int radius = int(round(r));
            array<NonUniformSample> nonUniformSamples;
            for(int3 octant: (int3[]){int3{-1,-1,-1},int3{1,-1,-1},int3{-1,1,-1},int3{1,1,-1},int3{-1,-1,1},int3{1,-1,1},int3{-1,1,1},int3{1,1,1}}) {
                int3 center = int3(size/2) + (size/4) * octant;
                Dict args = copy(arguments);
                args.insert("histogram-squaredRadius.crop"_);
                args.insert(String("histogram-squaredRadius.cylinder"_), str((int[]){center.x, center.y, radius, center.z-radius, center.z+radius},','));
                shared<Result> result = results.getResult("volume-distribution-radius"_, args); // Pore size distribution (values are volume in voxels)
                if(radius==52) PSD_octant.insert(str(octant), "#Pore size distribution versus octants (R=50)\n"_+copy(result->data));
                nonUniformSamples << parseNonUniformSample( result->data );
            }
            array<UniformSample> samples = resample(nonUniformSamples);
            UniformHistogram mean = ::mean(samples);
            if(radius==52) PSD_octant.insert(String("mean"_), "#Pore size distribution versus octants (R=50)\n"_+toASCII(mean));
            uint median = mean.median();
            for(uint i: range(3)) { /// Computes deviation of a sample of distributions
                uint begin = (uint[]){0,0,median}[i], end = (uint[]){(uint)mean.size,median,(uint)mean.size}[i]; // 0-size, 0-median, median-size
                assert_(begin<end);
                array<UniformSample> slices = apply(samples, [&](const UniformSample& sample){ return UniformSample(sample.slice(begin,end-begin)); } );
                UniformSample mean = ::mean(slices);
                real sumOfSquareDifferences = 0; for(const UniformSample& slice: slices) sumOfSquareDifferences += sum(sq(slice-mean));
                real unbiasedVarianceEstimator = sumOfSquareDifferences / (slices.size-1);  //assuming samples are uncorrelated
                real relativeDeviation = sqrt( unbiasedVarianceEstimator ) / sqrt( sq(mean).sum() );
                relativeDeviations[i].insert(radius, relativeDeviation);
            }
            PSD_R.insert("R="_+dec(radius,3), ("#Pore size distribution versus cylinder radius (mean of 8 volume samples)\n"_ + toASCII((1./mean.sampleCount())*mean)));
        }
        //TODO: scale R vx -> μm
        assert_(outputs[0]->name == "ε(R)"_);
        outputs[0]->metadata = String("ε(R).tsv"_);
        outputs[0]->data = "#Relative deviation of pore size distribution versus cylinder radius of 8 volume samples\n"_ + toASCII(relativeDeviations[0]);
        assert_(outputs[1]->name == "ε(R|r<median)"_);
        outputs[1]->metadata = String("ε(R).tsv"_);
        outputs[1]->data = "#Relative deviation of pore size distribution versus cylinder radius of 8 volume samples\n"_ + toASCII(relativeDeviations[1]);
        assert_(outputs[2]->name == "ε(R|r>median)"_);
        outputs[2]->metadata = String("ε(R).tsv"_);
        outputs[2]->data = "#Relative deviation of pore size distribution versus cylinder radius of 8 volume samples\n"_ + toASCII(relativeDeviations[2]);
        assert_(outputs[3]->name == "PSD(R)"_);
        outputs[3]->metadata = String("V(r).tsv"_);
        outputs[3]->elements = move(PSD_R);
        assert_(outputs[4]->name == "PSD(octant)"_);
        outputs[4]->metadata = String("V(r).tsv"_);
        outputs[4]->elements = move(PSD_octant);
    }
};
