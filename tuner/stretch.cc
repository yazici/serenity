#include "thread.h"
#include "math.h"
#include "pitch.h"
#include "data.h"
#include "time.h"
#include "ui/layout.h"
#include "window.h"
#include "graphics.h"
#include "text.h"
#include <fftw3.h> //fftw3f

/// Visualizes data generated by \a PitchEstimation
struct StretchEstimation : Widget {
    // UI
	Window window {this, int2(1024), [this]{return str(sB);}};

    // Key-specific pitch analysis data
    struct KeyData {
        struct Pitch { float F0, B; };
        array<Pitch> pitch; // Pitch estimation results where this key was detected
        buffer<float> spectrum; // Sum of all power spectrums where this key was detected
    };
    map<int, KeyData> keys;

	float sB = 54, sT = 129;
	float& variable = sB;

    StretchEstimation() {
        // Reads analysis data
        Folder stretch("/var/tmp/stretch"_);
        for(string file: stretch.list(Files)) {
            if(!endsWith(file, "f0B"_)) continue;
            string name = section(file,'.');
            KeyData data;
            data.pitch = cast<KeyData::Pitch>(readFile(name+".f0B"_, stretch));
            data.spectrum = cast<float>(readFile(name+".PSD"_, stretch));
			assert_(data.spectrum.size == 32768/2);
			keys.insertSorted(parseKey(name)-21, move(data));
        }

		window.background=Window::Black; //FIXME: additiveBlend = true;
		window.actions[KP_Minus] = [this]{variable--; window.render(); window.setTitle(str(variable));};
		window.actions[KP_Plus] = [this]{variable++; window.render(); window.setTitle(str(variable));};
        window.show();
    }

	float stretch(int m) { return -exp((-sB - m)/12.) + exp((m - sT)/12.); }
	//float stretch(int) { return 0; }

	int2 sizeHint(int2) { return int2(-keys.size()); }
	shared<Graphics> graphics(int2 size) override {
		shared<Graphics> graphics;
        range compass(keys.keys.first(), keys.keys.last());
        for(int key: compass) {
			uint x0 = (key       - compass.start) * size.x / (compass.stop - compass.start);
			uint x1 = (key +1 - compass.start) * size.x / (compass.stop - compass.start);
            const float scale = 4;
			auto render = [&](int key, float harmonicRatio, bgr3f color) {
                if(!keys.contains(key)) return;
                ref<float> power = keys.at(key).spectrum;
				float meanPower = sum(power, 0.) / power.size;
				assert_(meanPower);
                ref<KeyData::Pitch> s = keys.at(key).pitch;
                float meanF1; {float sum = 0; for(KeyData::Pitch p: s) sum += p.F0*(1+p.B); meanF1 = sum / s.size;}
				float maxPower = meanPower;
				for(uint n: range(1, 3 +1)) { // For each partial
                    int N = harmonicRatio*n;
                    int y0 = (3-n)*size.y/3, y1 = (3-n+1)*size.y/3, y12 = (y0+y1)/2;
                    for(uint f: range(1, power.size)) {
						float y = y12 - log2((float) f / N / meanF1 * exp2(stretch(key))) * size.y;
						if(y>y0 && y<y1) maxPower = max(maxPower, power[f]);
					}
				}
				for(uint n: range(1, 3 +1)) {
					int N = harmonicRatio*n;
					int y0 = (3-n)*size.y/3, y1 = (3-n+1)*size.y/3, y12 = (y0+y1)/2;
                    float sum = 0;
					for(KeyData::Pitch p: s) { // Plots partial frequency deduced from each estimation
                        float f = p.F0*(N+p.B*cb(N));
                        float y = y12 - log2(f/N /meanF1 * exp2(stretch(key))) * size.y * scale;
                        if(y>y0 && y<y1 && round(f)<power.size) {
							float p = power[round(f)]; // Scales intensity with measured power of given partial in nearest bin to estimated frequency
							float intensity = (p  > meanPower ? log2(p / meanPower) / log2(maxPower / meanPower) : 0) / s.size; //TODO: ATH scale
							graphics->lines.append(vec2(x0, y), vec2(x1, y), color, intensity);
                        }
                        sum += f;
                    }
					// Plots mean partial frequency
                    float f = sum / s.size;
                    float y = y12 - log2(f/N /meanF1 * exp2(stretch(key))) * size.y * scale;
                    if(y>y0 && y<y1 && round(f)<power.size) {
						float p = power[round(f)]; // Scales intensity with measured power of given partial in nearest bin to mean frequency
						float intensity = p  > meanPower ? log2(p / meanPower) / log2(maxPower / meanPower) : 0;
						graphics->lines.append(vec2(x0, y), vec2(x1, y), color, intensity);
                    }
                }
            };
			render(key, 1, green); // Plots highest note
			render(key-12, 2, red); // Plots lower octave (1:2)
			render(key-24, 4, blue); // Plots twice lower octave (1:3)
        }
		return graphics;
    }
} app;

