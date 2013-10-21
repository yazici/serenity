#include "thread.h"
#include "sampler.h"
#include "math.h"
#include <fftw3.h> //fftw3f
typedef struct fftwf_plan_s* fftwf_plan;
struct FFTW : handle<fftwf_plan> { using handle<fftwf_plan>::handle; default_move(FFTW); FFTW(){} ~FFTW(); };
FFTW::~FFTW() { if(pointer) fftwf_destroy_plan(pointer); }
struct FFT {
    uint N;
    buffer<float> hann {N};
    buffer<float> windowed {N};
    buffer<float> halfcomplex {N};
    FFTW fftw = fftwf_plan_r2r_1d(N, windowed.begin(), halfcomplex.begin(), FFTW_R2HC, FFTW_ESTIMATE);
    FFT(uint N) : N(N) { for(uint i: range(N)) hann[i] = (1-cos(2*PI*i/(N-1)))/2; }
    ref<float> transform(const ref<float>& signal) {
        assert(N <= signal.size);
        for(uint i: range(N)) windowed[i] = hann[i]*signal[i]; // Multiplies window
        fftwf_execute(fftw); // Transforms (FIXME: use execute_r2r and free windowed/transform buffers between runs)
        return halfcomplex;
    }
};

template<Type V, uint N> struct list { // Small sorted list
    static constexpr uint size = N;
    struct element { float key; V value; element(float key=0, V value=0):key(key),value(value){} } elements[N];
    void insert(float key, V value) {
        int i=0; while(i<N && elements[i].key<key) i++; i--;
        if(i<0) return; // New candidate would be lower than current
        for(uint j: range(i)) elements[j]=elements[j+1]; // Shifts left
        elements[i] = element(key, value); // Inserts new candidate
    }
    const element& operator[](uint i) { assert(i<N); return elements[i]; }
    const element& last() { return elements[N-1]; }
    const element* begin() { return elements; }
    const element* end() { return elements+N; }
};
template<Type V, uint N> String str(const list<V,N>& a) {
    String s; for(uint i: range(a.size)) { s<<str(a.elements[i].key, a.elements[i].value); if(i<a.size-1) s<<", "_;} return s;
}

/// Numeric range
struct reverse_range {
    reverse_range(range r) : start(r.stop), stop(r.start){}
    struct iterator {
        int i;
        int operator*() { return i; }
        iterator& operator++() { i--; return *this; }
        bool operator !=(const iterator& o) const{ return i>=o.i; }
    };
    iterator begin() const { return {start}; }
    iterator end() const { return {stop}; }
    int start, stop;
};

/// Reverse iterator
generic struct reverse {
    reverse(const ref<T>& a) : start(a.end()-1), stop(a.begin()) {}
    struct iterator {
        const T* pointer;
        const T& operator*() { return *pointer; }
        iterator& operator++() { pointer--; return *this; }
        bool operator !=(const iterator& o) const { return pointer>=o.pointer; }
    };
    iterator begin() const { return {start}; }
    iterator end() const { return {stop}; }
    const T* start;
    const T* stop;
};

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }

/// Estimates fundamental frequency (~pitch) of test samples
struct PitchEstimation {
    PitchEstimation() {
        Sampler sampler;
        const uint sampleRate = 48000;
        sampler.open(sampleRate, "Salamander.sfz"_, Folder("Samples"_,root()));

        const uint N = 32768; // Analysis window size (16 periods of A-1)
        FFT fft (N);

        const bool singleVelocity = true; // Tests 30 samples or 30x16 samples (TODO: fix failing low/high velocities)
        uint tests = 0;
        for(const Sample& sample: sampler.samples) {
            if(sample.trigger!=0) continue;
            if(singleVelocity && (sample.lovel > 64 || 64 > sample.hivel)) continue;
            tests++;
        }

        uint result[tests]; // Rank of actual pitch within estimated candidates
        clear(result, tests, uint(~0));
        uint testIndex=0;
        for(const Sample& sample: sampler.samples) {
            if(sample.trigger!=0) continue;
            if(singleVelocity && (sample.lovel > 64 || 64 > sample.hivel)) continue;
            int expectedKey = sample.pitch_keycenter;

            assert(N<=sample.flac.duration);
            buffer<float2> stereo = decodeAudio(sample.data, N);
            float signal[N];
            for(uint i: range(N)) signal[i] = (stereo[i][0]+stereo[i][1])/(2<<(24+1));
            ref<float> halfcomplex = fft.transform(signal);
            buffer<float> spectrum (N/2);
            for(int i: range(N/2)) spectrum[i] = sq(halfcomplex[i]) + sq(halfcomplex[N-1-i]); // Converts to intensity spectrum
            // Estimates candidates using maximum peak
            const uint fMin = 18; // ~27 Hz ~ semi pitch under A-1
            const uint fMax = 2941; // ~4186 Hz ~ semi pitch over C7
            int peak=0; float peakMax=0; for(uint i=fMin; i<=fMax; i++) if(spectrum[i]>peakMax) peakMax=spectrum[i], peak=i;
            // Use autocorrelation to find best match between f, f/2, f/3, f/4
            float ncc = peak;
            const int kMax = round(4.*N/peak);
            float nccMax=0;
            if(peak < N/32.) { // High pitches don't work well with autocorrelation
                for(uint i=1; i<=4; i++) {
                    int k = round((float)i*N/peak);
                    float ec=0; for(uint i: range(N-kMax)) ec += signal[i]*signal[k+i]; // Correlation
                    if(ec > nccMax) nccMax = ec, ncc = ncc/i;
                }
            }
            int key = round(pitchToKey((float)sampleRate*ncc/N)); // 11 samples rounds to #C7
            if(key==expectedKey) result[testIndex] = 0;
            else {
                log(">", expectedKey, keyToPitch(expectedKey)/sampleRate*N);
                log("?", peakMax, peak, nccMax, ncc, key);
            }
            testIndex++;
        }
        int success[4] = {};
        buffer<char> detail(tests);
        for(int i : range(tests)) {
            int rank = result[i];
            for(uint j: range(4)) success[j] += uint(rank)<j+1;
            assert_(rank>=-1 && rank<8, rank);
            detail[i] = "X12345678"[rank+1];
        }
        String s;
        for(uint j: range(4)) if(j==0 || success[j-1]<success[j]) s<<str(j+1)+": "_<<str(success[j])<<", "_; s.pop(); s.pop();
        log(detail, "("_+s+")"_);
    }
} test;
