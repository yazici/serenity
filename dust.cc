#include "parallel.h"

// -> image.cc
#include "image.h"
/// Convolves and transposes (with repeat border conditions)
void convolve(float* target, const float* kernel, int radius, const float* source, int width, int height) {
    int N = radius+1+radius;
    chunk_parallel(height, [&](uint, uint y) {
        const float* line = source + y * width;
        float* targetColumn = target + y;
        for(int x: range(-radius,0)) {
            float sum = 0;
            for(int dx: range(N)) sum += kernel[dx] * line[max(0, x+dx)];
            targetColumn[(x+radius)*height] = sum;
        }
        for(int x: range(0,width-2*radius)) {
            float sum = 0;
            const float* span = line + x;
            for(int dx: range(N)) sum += kernel[dx] * span[dx];
            targetColumn[(x+radius)*height] = sum;
        }
        for(int x: range(width-2*radius,width-radius)){
            float sum = 0;
            for(int dx: range(N)) sum += kernel[dx] * line[min(width-1, x+dx)];
            targetColumn[(x+radius)*height] = sum;
        }
    });
}

float gaussian(float sigma, float x) { return exp(-sq(x)/(2*sq(sigma))); }
ImageF gaussianBlur(ImageF&& target, const ImageF& source, float sigma) {
    int radius = ceil(3*sigma), N = radius+1+radius;
    float kernel[N];
    for(int dx: range(N)) kernel[dx] = gaussian(sigma, dx-radius); // Sampled gaussian kernel (FIXME)
    float energy = sum(ref<float>(kernel,N)); mref<float>(kernel,N) *= 1/energy;
    buffer<float> transpose (target.height*target.width);
    convolve(transpose, kernel, radius, source.pixels, source.width, source.height);
    convolve(target.pixels, kernel, radius, transpose, target.height, target.width);
    return move(target);
}
ImageF gaussianBlur(const ImageF& source, float sigma) { return gaussianBlur(source.size, source, sigma); }

/// \file dust.cc Automatic dust removal
#include "filter.h"

/// Inverts attenuation bias
struct InverseAttenuation : Filter {
    ImageSource attenuationBias;

    InverseAttenuation(const ImageFolder& calibrationImages) : attenuationBias(calibrateAttenuationBias(calibrationImages)) {}

    /// Calibrates attenuation bias image by summing images of a white subject
    ImageSource calibrateAttenuationBias(const ImageFolder& calibration) {
        if(1 || skipCache || !existsFile("attenuationBias"_, calibration.cacheFolder)) {
            ImageTarget attenuationBias ("attenuationBias"_, calibration.cacheFolder, calibration.imageSize);
            if(existsFile("attenuationBias.lock"_)) remove("attenuationBias.lock"_);
            rename("attenuationBias"_, "attenuationBias.lock"_, calibration.cacheFolder);
            log("Calibration");

            attenuationBias.pixels.clear();
            for(string imageName: calibration.keys) {
                ImageSource source = calibration.image(imageName, Mean); // Assumes dust affects all components equally
                parallel_apply(attenuationBias.pixels, attenuationBias.pixels, source.pixels, [](float sum, float source) { return sum + source; });
            }
            // Low pass to filter texture and noise
            // gaussianBlur(attenuationBias, 16); // TODO: weaken near spot, strengthen outside
            // TODO: High pass to filter lighting conditions
            normalize(attenuationBias.pixels); // Normalizes sum by maximum (TODO: normalize by low frequency energy)
            rename("attenuationBias.lock"_, "attenuationBias"_, calibration.cacheFolder);
        }
        return ImageSource("attenuationBias"_, calibration.cacheFolder, calibration.imageSize);
    }

    ImageSource image(const ImageFolder& imageFolder, string imageName, Component component) const override {
        Folder targetFolder = Folder(".target"_, imageFolder.cacheFolder, true);
        String id = imageName+"."_+str(component);
        if(1 || skipCache || !existsFile(id, targetFolder)) {
            ImageSource source = imageFolder.image(imageName, component);
            int2 size = source.ImageF::size;

            // TODO: reuse buffers
            ImageF low_spot = gaussianBlur(source, 1);
            ImageF high (size);
            subtract(high.pixels, source.pixels, low_spot.pixels);
            ImageF low = gaussianBlur(low_spot, 64); // 128 is too slow ... (TODO: fast large radius blur)
            ImageF spot (size);
            subtract(spot.pixels, low_spot.pixels, low.pixels);

            ImageF correction_unweighted (size);
            float DC = parallel_sum(source.pixels) / source.pixels.size;
            parallel_apply(correction_unweighted.pixels, spot.pixels, attenuationBias.pixels, [&](float source, float bias) { return (DC + source) / bias - DC; });

            copy(correction_unweighted.pixels, spot.pixels);
            ImageF correction_low_spot = gaussianBlur(correction_unweighted, 8);
            ImageF correction_high (size);
            subtract(correction_high.pixels, correction_unweighted.pixels, correction_low_spot.pixels);
            ImageF correction_low = gaussianBlur(low_spot, 16);
            ImageF correction_spot (size);
            subtract(correction_spot.pixels, correction_low_spot.pixels, correction_low.pixels);

            ImageF correction (size);
            copy(correction.pixels, correction_spot.pixels); //TODO: weight
            add(correction.pixels, correction.pixels, correction_low.pixels);
            add(correction.pixels, correction.pixels, correction_high.pixels);

            ImageTarget target (id, targetFolder, size);
            copy(target.pixels, correction.pixels);
            add(target.pixels, target.pixels, low.pixels);
            add(target.pixels, target.pixels, high.pixels);

            //copy(target.pixels, spot.pixels);
            //target.pixels += DC;
            //target.pixels += -minimum(target.pixels);
            //normalize(target.pixels);
        }
        return ImageSource(id, targetFolder, imageFolder.imageSize);
    }
};

#include "interface.h"

struct ImageView : ImageWidget {
    const ImageFolder& images;
    string imageName = images.keys[0];
    ImageSourceRGB source; // Holds memory map reference
    function<void(string, const map<String, String>&)> contentChanged;

    ImageView(const ImageFolder& images, decltype(contentChanged) contentChanged)
        : images(images), contentChanged(contentChanged) { update(); }

    bool mouseEvent(int2 cursor, int2 size, Event, Button, Widget*&) override {
        string imageName = images.keys[images.size()*min(size.x-1,cursor.x)/size.x];
        if(imageName != this->imageName) {
            this->imageName = imageName; update();
            contentChanged(imageName, images.at(imageName));
            return true;
        }
        return false;
    }

    virtual void update() {
        source = images.scaledRGB(imageName);
        ImageWidget::image = share(source);
    }
};

struct FilterView : ImageView {
    const Filter& filter;
    bool enabled = false;

    FilterView(const ImageFolder& images, decltype(contentChanged) contentChanged, const Filter& filter)
        : ImageView(images, contentChanged), filter(filter) { update(); }

    bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) override {
        bool enabled = button != NoButton && event != Release;
        if(enabled != this->enabled) { this->enabled = enabled, imageName=""_;/*Forces update*/ }
        return ImageView::mouseEvent(cursor, size, event, button, focus);
    }

    void update() override {
        if(enabled) ImageWidget::image = sRGB(
                    filter.image(images, imageName, Blue),
                    filter.image(images, imageName, Green),
                    filter.image(images, imageName, Red) );
        else ImageView::update();
    }
};

#include "window.h"

struct DustRemovalPreview {
    ImageFolder calibrationImages = Folder("Pictures/Paper"_, home());
    //ImageView view {calibrationImages};
    InverseAttenuation filter {calibrationImages};
    //ImageWidget view {sRGB(filter.attenuationBias)};
    ImageFolder images {Folder("Pictures"_, home())};
    FilterView view {calibrationImages, [&](string name, const map<String, String>& tags){ window.setTitle(str(name, tags)); }, filter};
    Window window {&view};
} application;
