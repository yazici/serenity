#include "volume-operation.h"
#include "sample.h"
#include "time.h"
#include "thread.h"
#include "simd.h"

/// Square roots samples
UniformSample<double> squareRoot(const UniformSample<double>& A) {
    uint N=A.size; UniformSample<double> R(N);
    for(uint i: range(N)) { R[i]=sqrt(A[i]); assert(!__builtin_isnanf(R[i]) && R[i]!=__builtin_inff()); }
    return R;
}

/// Exhaustively search for inter-class variance maximum ω₁ω₂(μ₁ - μ₂)² (shown by Otsu to be equivalent to intra-class variance minimum ω₁σ₁² + ω₂σ₂²)
class(Otsu, Operation) {
    void execute(const Dict&, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        assert_(inputs[0]->metadata == "histogram.tsv"_);
        UniformHistogram density = parseUniformSample<int64>( inputs[0]->data );
        density[0]=density[density.size-1]=0; // Ignores clipped values
        uint threshold=0; double maximumVariance=0;
        uint64 totalCount=0, totalSum=0;
        for(uint64 t: range(density.size)) totalCount+=density[t], totalSum += t * density[t];
        uint64 backgroundCount=0, backgroundSum=0;
        UniformSample<double> interclassVariance(density.size); double parameters[4];
        for(uint64 t: range(density.size)) {
            backgroundCount += density[t];
            if(backgroundCount == 0) continue;
            backgroundSum += t*density[t];
            uint64 foregroundCount = totalCount - backgroundCount, foregroundSum = totalSum - backgroundSum;
            if(foregroundCount == 0) break;
            double foregroundMean = double(foregroundSum)/double(foregroundCount);
            double backgroundMean = double(backgroundSum)/double(backgroundCount);
            double variance = double(foregroundCount)*double(backgroundCount)*sq(foregroundMean - backgroundMean);
            if(variance > maximumVariance) {
                maximumVariance=variance, threshold = t;
                parameters[0]=backgroundCount, parameters[1]=foregroundCount, parameters[2]=backgroundMean, parameters[3]=foregroundMean;
            }
            interclassVariance[t] = variance;
        }
        float densityThreshold = float(threshold) / float(density.size-1);
        //log("Otsu's method estimates threshold at", densityThreshold);
        outputs[0]->metadata = String("scalar"_);
        outputs[0]->data = ftoa(densityThreshold, 6)+"\n"_;
        output(outputs, 1, "map"_, [&]{
            return "threshold "_+ftoa(densityThreshold, 6)+"\n"_
                    "threshold16 "_+dec(threshold)+"\n"_
                    "maximum "_+dec(density.size-1)+"\n"_
                    "backgroundCount "_+dec(parameters[0])+"\n"_
                    "foregroundCount "_+dec(parameters[1])+"\n"_
                    "backgroundMean "_+str(parameters[2])+"\n"_
                    "foregroundMean "_+str(parameters[3])+"\n"_
                    "maximumDeviation "_+str(sqrt(maximumVariance/sq(totalCount)))+"\n"_; } );
        output(outputs, 2, "deviation.tsv"_, [&]{ return toASCII((1./(totalCount-1))*squareRoot(interclassVariance)); } );
    }
};

#if 1
// Lorentz
/// Substracts samples clipping to zero
generic UniformSample<T> operator-(const UniformSample<T>& A, const UniformSample<T>& B) {
    uint N=A.size; assert(B.size==N); UniformSample<T> R(N);
    for(uint i: range(N)) R[i]=max<T>(0, A[i]-B[i]);
    return R;
}

/// Cauchy-Lorentz distribution 1/(1+x²)
struct Lorentz {
    double position, height, scale;
    double operator()(float x) const { return height/(1+sq((x-position)/scale)); }
};
template<> inline String str(const Lorentz& o) { return "x₀ "_+str(o.position)+", I"_+str(o.height)+", γ "_+str(o.scale); }

/// Estimates parameters for a Lorentz distribution fitting the maximum peak
Lorentz estimateLorentz(const UniformSample<int64>& sample) {
    Lorentz lorentz;
    uint x0=0; for(uint x=0; x<sample.size; x++) if(sample[x]>sample[x0]) x0=x;
    uint y0 = sample[x0];
    uint l0=0; for(int x=x0; x>=0; x--) if(sample[x]<=y0/2) { l0=x; break; } // Left half maximum
    uint r0=sample.size; for(int x=x0; x<(int)sample.size; x++) if(sample[x]<=y0/2) { r0=x; break; } // Right half maximum
    lorentz.position = max(x0, (l0+r0)/2); // Position estimated from half maximum is probably more accurate
    lorentz.scale = (r0-l0)/2; // half-width at half-maximum (HWHM)
    lorentz.height = y0;
    return lorentz;
}
/// Evaluates a Lorentz distribution at regular intervals
UniformSample<int64> sample(const Lorentz& lorentz, uint size) {
    UniformSample<int64> sample(size);
    for(int x=0; x<(int)size; x++) sample[x] = lorentz(x);
    return sample;
}
/// Lorentzian peak mixture estimation. Works for well separated peaks (intersection under half maximum), proper way would be to use expectation maximization
class(LorentzianMixtureModel, Operation) {
    void execute(const Dict&, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        assert_(inputs[0]->metadata == "histogram.tsv"_);
        UniformHistogram density = parseUniformSample<int64>( inputs[0]->data );
        density[0]=density[density.size-1]=0; // Zeroes extreme values (clipping artifacts)
        const Lorentz rock = estimateLorentz(density); // Rock density is the highest peak
        const UniformSample<int64> notrock = density - sample(rock, density.size); // Substracts first estimated peak in order to estimate second peak
        Lorentz pore = estimateLorentz(notrock); // Pore density is the new highest peak
        pore.height = density[pore.position]; // Use peak height from total data (estimating on not-rock yields too low estimate because rock is estimated so wide its tail overlaps pore peak)
        const UniformSample<int64> notpore = density - sample(pore, density.size);
        uint threshold=0; for(uint i: range(pore.position, rock.position)) if(pore(i) <= notpore[i]) { threshold = i; break; } // First intersection between pore and not-pore (same probability)
        float densityThreshold = float(threshold) / float(density.size);
        log("Lorentzian mixture model estimates threshold at", densityThreshold, "between pore at", float(pore.position)/float(density.size), "and rock at", float(rock.position)/float(density.size));
        outputs[0]->metadata = String("scalar"_);
        outputs[0]->data = ftoa(densityThreshold, 5)+"\n"_;
        output(outputs, 1, "lorentz"_, [&]{ return str("rock",rock)+"\n"_+str("pore",pore); });
        output(outputs, 2, "lorentz.tsv"_, [&]{ return toASCII(sample(rock,density.size)); });
        output(outputs, 3, "density.tsv"_, [&]{ return toASCII(notrock); });
        output(outputs, 4, "lorentz.tsv"_, [&]{ return toASCII(sample(pore,density.size)); });
        output(outputs, 5, "density.tsv"_, [&]{ return toASCII(notpore); });
    }
};
#endif

#if 1
/// Computes the mean gradient for each set of voxels with the same density, and defines threshold as the density of the set of voxels with the maximum mean gradient
/// \note Provided for backward compatibility only
class(MaximumMeanGradient, Operation) {
    void execute(const Dict&, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        const Volume16& source = toVolume(*inputs[0]);
        uint X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
        UniformSample<int64> gradientSum (source.maximum+1, source.maximum+1, 0); // Sum of finite differences for voxels belonging to each density value
        UniformHistogram histogram (source.maximum+1, source.maximum+1, 0); // Count samples belonging to each class to compute mean
        for(uint z: range(Z-1)) for(uint y: range(Y-1)) for(uint x: range(X-1)) {
            const uint16* const voxel = &source[z*X*Y+y*X+x];
            uint gradient = abs(int(voxel[0]) - int(voxel[1])) + abs(int(voxel[0]) - int(voxel[X])) /*+ abs(voxel[0] - voxel[X*Y])*/; //[sic] Anistropic for backward compatibility
            const uint binCount = 255; assert(binCount<=source.maximum); // Quantizes to 8bit as this method fails if voxels are not grouped in large enough sets
            uint bin = uint(voxel[0])*binCount/source.maximum*source.maximum/binCount;
            gradientSum[bin] += gradient, histogram[bin]++;
            //gradientSum[voxel[1]] += gradient, histogram[voxel[1]]++; //[sic] Asymetric for backward compatibility
            //gradientSum[voxel[X]] += gradient, histogram[voxel[X]]++; //[sic] Asymetric for backward compatibility
            //gradientSum[voxel[X*Y]] += gradient, histogram[voxel[X*Y]]++; //[sic] Asymetric for backward compatibility
        }
        // Pick pore and rock space as the two highest density maximums (FIXME: is this backward compatible ?)
        UniformSample<double> density = parseUniformSample<double>( inputs[1]->data );
        uint pore=0, rock=0; double poreMaximum=0, rockMaximum=0;
        for(uint i: range(1,density.size-1)) {
            if(density[i-1]<density[i] && density[i]<density[i+1] && density[i]>poreMaximum) {
                pore=i; poreMaximum = density[i];
                if(poreMaximum > rockMaximum) swap(pore, rock), swap(poreMaximum, rockMaximum);
            }
        }
        log((double)pore/histogram.size, (double)rock/histogram.size);
        assert(rock > pore); assert(rockMaximum>poreMaximum); assert(rock < histogram.size);
        uint threshold=0; double maximum=0;
        UniformSample<int64> gradientMean (source.maximum+1);
        for(uint i: range(pore, rock)) {
            if(!histogram[i]) continue; // Not enough samples to properly estimate mean gradient for this density threshold
            double mean = gradientSum[i]/histogram[i];
            if(mean>maximum) maximum = mean, threshold = i;
            gradientMean[i] = mean;
        }
        double densityThreshold = (double)threshold/histogram.size;
        log("Maximum mean gradient estimates threshold at", densityThreshold, "with mean gradient", maximum, "defined by", dec(histogram[threshold]), "voxels");
        outputs[0]->metadata = String("scalar"_);
        outputs[0]->data = ftoa(densityThreshold, 5)+"\n"_;
        output(outputs, 1, "gradient-mean.tsv"_, [&]{ return toASCII(gradientMean); } );
    }
};
#endif

/// Segments by setting values under a fixed threshold to ∞ (2³²-1) and to x² otherwise (for distance X input)
void threshold(Volume32& pore, /*Volume32& rock,*/ const Volume16& source, uint16 threshold, bool cylinder=false) {
    // Ensures threshold volume is closed to avoid null/full rows in aligned distance search
    int marginX=align(4,source.margin.x)+1, marginY=align(4,source.margin.y)+1, marginZ=align(4,source.margin.z)+1;
    v4si threshold4 = set1(threshold);
    int X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY=X*Y;
    assert_(X%8==0 && (!cylinder || (X%2==0 && Y%2==0 && X-2*marginX==Y-2*marginY)));
    uint radiusSq = cylinder ? (X/2-marginX)*(Y/2-marginY) : -1;
    uint32 sqr[X]; for(int x=0; x<X; x++) sqr[x]=x*x; // Lookup table of squares
    uint32 mask[X*Y]; // Disk mask
    for(int y=0; y<Y; y++) for(int x=0; x<X; x++) mask[y*X+x]= (y<marginY || y>=Y-marginY || x<marginX || x>=X-marginX || uint(sq(y-Y/2)+sq(x-X/2)) > radiusSq) ? 0 : 0xFFFFFFFF;
    uint32* const poreData = pore;
    parallel(Z, [&](uint, int z) {
        const uint16* const sourceZ = source + z*XY;
        uint32* const poreZ = poreData + z*XY;
        if(z < marginZ || z>=Z-marginZ) for(int y=0; y<Y; y++) for(int x=0; x<X; x+=4) storea(poreZ+y*X+x, loada(sqr+x));
        else for(int y=0; y<Y; y++) {
            const uint16* const sourceY = sourceZ + y*X;
            uint32* const poreZY = poreZ + y*X;
            uint32* const maskY = mask + y*X;
            for(int x=0; x<X; x+=8) {
                storea(poreZY+x, loada(sqr+x) | ((threshold4 > unpacklo(loada(sourceY+x), _0h)) & loada(maskY+x)) );
                storea(poreZY+x+4, loada(sqr+x+4) | ((threshold4 > unpackhi(loada(sourceY+x), _0h)) & loada(maskY+x+4)) );
            }
        }
    });
    pore.margin.x=marginX, pore.margin.y=marginY, pore.margin.z=marginZ;
#if ASSERT
    pore.maximum=0xFFFFFFFF; // for the assert
#else
    pore.maximum=(pore.sampleCount.x-1)*(pore.sampleCount.x-1); // for visualization
#endif
}

/// Segments between either rock or pore space by comparing density against a uniform threshold
class(Binary, Operation), virtual VolumeOperation {
    string parameters() const override { return "threshold cylinder"_; }
    uint outputSampleSize(uint) override { return sizeof(uint); }
    void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>& otherInputs) override {
        uint16 binaryThreshold;
        if(args.contains("threshold"_) && isDecimal(args.at("threshold"_))) {
            float threshold = toDecimal(args.at("threshold"_));
            if(threshold < 1) binaryThreshold = round( threshold*inputs[0].maximum ); // Normalized (/maximum)
            else if(threshold < 256) binaryThreshold = round(threshold*(1<<8)); // 8bit -> 16bit
            else binaryThreshold = round(threshold); // 16bit
        } else {
            Result* threshold = otherInputs[0];
            binaryThreshold = round( TextData(threshold->data).decimal() * inputs[0].maximum );
        }
        threshold(outputs[0], /*outputs[1],*/ inputs[0], binaryThreshold, args.value("cylinder"_,"0"_)!="0"_);
    }
};

/// Maps intensity to either red or green channel depending on binary classification
void colorize(Volume24& target, const Volume32& binary, const Volume16& intensity) {
    assert_(!binary.tiled() && !intensity.tiled() && binary.sampleCount == intensity.sampleCount);
    const uint maximum = intensity.maximum;
    chunk_parallel(binary.size(), [&](uint offset, uint size) {
        const uint32* const binaryData = binary + offset;
        const uint16* const intensityData = intensity + offset;
        bgr* const targetData = target + offset;
        for(uint i : range(size)) {
            uint8 c = 0xFF*intensityData[i]/maximum;
            targetData[i] = binaryData[i]==0xFFFFFFFF ? bgr{0,c,0} : bgr{0,0,c};
        }
    });
    target.maximum=0xFF;
}

class(Colorize, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return 3; }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override { colorize(outputs[0], inputs[0], inputs[1]); }
};
