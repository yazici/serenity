#include "pitch.h"
#include "fft.h"
#include "sort.h"

float PitchEstimator::estimate() {
    {harmonicEnergy=0, F0=0, B=0, this->F1=0, this->nHigh=0;}
    candidates.clear();

    ref<float> spectrum = transform();
    for(uint i: range(0, fMax)) filteredSpectrum[i] = 0;

    peaks.clear();
    minF=fMax, maxF=fMin; uint last=0; uint F1=0; uint maxPeak=0;
    for(uint i: range(11/*19*/, fMax-2)) {
        if(spectrum[i- 1] < spectrum[i] && spectrum[i] > spectrum[i+1] && spectrum[i] > noiseThreshold*periodPower) {
            if(spectrum[i-2]/2 < spectrum[i-1] && spectrum[i+1] > spectrum[i+2]/2) {
                // Copies peaks / Filters non peaks
                filteredSpectrum[i] = spectrum[i];
                for(uint j=i-1; j>0 && spectrum[j+1]>spectrum[j] && j>i-3; j--) filteredSpectrum[j] = spectrum[j];
                for(uint j=i+1; j<fMax && spectrum[j-1]>spectrum[j] && j<i+3; j++) filteredSpectrum[j] = spectrum[j];
                // Records peaks
                if(i<minF) minF=i; if(i>maxF) maxF=i;
                if(i-fMin >= last) { peaks.insert(spectrum[i], {i}); } // Records maximum peaks
                else if(spectrum[i]>spectrum[last]) { // Overwrites lower peak
                    peaks.remove({last}); // Ensures only one is kept
                    peaks.insert(spectrum[i], {i});
                } // else skips lower peak
                last=i;
                if(spectrum[i] > maxPeak) maxPeak=spectrum[i], F1=i;
            }
        }
    }
    uint highPeak = 0;
    for(Peak peak: peaks.slice(max<int>(0,peaks.size-6)))
        if(peak.f > minHighPeak && spectrum[peak.f] > highPeakThreshold*periodPower) highPeak = peak.f;
    spectrum = filteredSpectrum; // Cleans spectrum
    if(!peaks.size) { harmonicEnergy=0; return 0; }
    array<uint> byFrequency(peaks.size);
    for(Peak peak: peaks) byFrequency.insertSorted(peak.f); // Insertion sorts by frequency
    array<uint> distance (peaks.size);
	{uint last=0; for(uint f: byFrequency) { distance.append(f-last); last=f; }} // Compute distances
    uint medianF0 = ::median(distance);
    // Corrects outlying fundamental estimate from median
    if(highPeak && highPeak/medianF0>=minHighPeakRank) {
        medianF0 = highPeak;
        for(const auto& peak: peaks.slice(max<int>(0,peaks.size-5))) {
            //log_(str(peak.f)+' ');
            if(peak.f >= highPeak*minHighPeakNum/minHighPeakDen && peak.f >= minHighPeakNum && peak.f<medianF0) medianF0=peak.f;
        }
        //log("\thighPeak ->", F1, medianF0, highPeak, highPeak*minHighPeakNum/minHighPeakDen);
        F1=highPeak;
    } else {
        if(byFrequency.last()/medianF0>=lastHarmonicRank && byFrequency.last()>=lastHarmonicFrequency
                && F1/medianF0>=peakRank && F1>=peakFrequency) {
            medianF0=F1;
            for(const auto& peak: peaks.slice(max<int>(0,peaks.size-6))) {
                if(peak.f >= F1*minNum/minDen && peak.f >= minNum && peak.f < medianF0) medianF0=peak.f;
            }
        }
    }

    uint nHigh = F1/max<int>(fMin, medianF0-medianError);
    this->F1=F1, this->medianF0 = medianF0, this->nHigh=nHigh;
    float bestEnergy = 0;
    for(uint n1: range(1, nHigh +1)) {
        float f0 = (float) F1 / (n1*(1 + initialInharmonicity*sq(n1))), f0B = f0*initialInharmonicity;
        float energy = 0; array<uint> peaks;
        for(uint t unused: range(1,iterationCount +1)) {
            peaks.clear(); energy = 0;
            // Least square optimization of linear fit: n.f0 = f[n] => argmin |Xb - y|^2 (X=n, b=f0, y=f[n]) <=> X'X b = X' y
            float n2=0, n3=0, n4=0, nf=0, n2f=0; // Least square X'X and X'y coefficients
            if(f0+f0B<fMin) f0=fMin;
            for(uint n=1; n<= maxHarmonicCount; n++) {
                // Finds local maximum around harmonic frequencies predicted by last f0 estimation
                int fn = round(f0*n + f0B*cb(n)); // Using Bn^2 instead of n*sqrt(1+Bn^2) in order to keep least square linear (assumes B<<1)
                if(fn+fMin/2>=fMax) break;
                uint f=fn; float peakEnergy = spectrum[f];
                uint df=1;
                for(; df<=fMin/8; df++) { // Fit nearest peak (FIXME: proportionnal to frequency)
                    if(spectrum[fn+df] > peakEnergy) peakEnergy=spectrum[fn+df], f=fn+df;
                    if(spectrum[fn-df] > peakEnergy) peakEnergy=spectrum[fn-df], f=fn-df;
                }
				peaks.append(f);
                energy += peakEnergy;
                for(; df<=fMin/2; df++) { // Fit nearest peak (FIXME: proportionnal to frequency)
                    if(spectrum[fn+df] > peakEnergy) peakEnergy=spectrum[fn+df], f=fn+df;
                    if(spectrum[fn-df] > peakEnergy) peakEnergy=spectrum[fn-df], f=fn-df;
                }
                float w = peakEnergy;
                nf  += w * n * f;
                n2 += w * n * n;
                n2f+= w * cb(n) * f;
                n3 += w * cb(n) * n;
                n4 += w * cb(n) * cb(n);
            }
            // Solves X'X b = X'y
            float a=n2, b=n3, c=n3, d=n4;
            float det = a*d-b*c;
            if(det) {
                f0B = (-c*nf + a*n2f) / det;
                if(f0B>0) f0 = (d*nf - b*n2f) / det;
                else if(n2) f0 = nf/n2, f0B=0;
            } else if(n2) f0 = nf/n2, f0B=0;
        }
        if(highPeak && f0 >= highPeak-1) energy += periodPower; // High peak boost
        if(f0) candidates.insert(energy, Candidate(f0, f0B/f0, energy, copy(peaks)));
        if(energy > bestEnergy) {
            bestEnergy = energy;
            this->F0 = f0;
            this->B = f0 ? f0B/f0 : 0;
        }
    }
    harmonicEnergy = bestEnergy;
    if(highPeak && this->F0 >= highPeak-1) harmonicEnergy = max(harmonicEnergy, periodEnergy/9);
    return this->F0*(1+this->B);
}
