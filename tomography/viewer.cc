#include "thread.h"
#include "cdf.h"
#include "view.h"
#include "project.h"
#include "layout.h"
#include "window.h"
#include "error.h"

#if 0
VolumeCDF projectionData (Folder("Preprocessed"_ , Folder("Data"_, home())));
VolumeCDF reconstruction (Folder("FBP"_, Folder("Results"_, home())));

buffer<ImageF> images = sliceProjectionVolume(projectionData.volume);
buffer<Projection> projections = evaluateProjections(reconstruction.volume.sampleCount, images[0].size(), images.size);

ProjectionView projectionsView (images, 2);
//DiffView diff (&reconstruction.volume, &projections.volume);
VolumeView reconstructionView (reconstruction.volume, projections, 2);
HBox views ({ &projectionsView, /*&diff,*/ &reconstructionView });
//const int2 imageSize = int2(504, 378);
Window window (&views, "View"_);
#elif 1

struct Viewer : HBox {
    buffer<Map> files;
    buffer<VolumeF> volumes;
    uint currentIndex = 0;
    const VolumeF* current() { return &volumes[currentIndex]; }
    buffer<Projection> projections = evaluateProjections(current()->sampleCount, int2(256,256), 256); //evaluateProjections(current()->sampleCount, int2(504,378), 5041);
    SliceView sliceView {current(), 2};
    VolumeView volumeView {current(), projections, 2};
    Window window;

    Viewer(ref<string> paths) :
        HBox{{ &sliceView , &volumeView }},
        files( apply(paths,[](string path){ return Map(path); } ) ),
        volumes ( apply(paths.size, [&](uint i){ return VolumeF(parseSize(paths[i]), cast<float>((ref<byte>)files[i])); }) ),
        window (this, join(apply(paths,[](string path){ return section(section(path,'/',-2,-1),'.'); })," "_)) {
        if(volumes.size == 2) {
            const float MSE = SSE(volumes[0], volumes[1])/sqrt(SSQ(volumes[0])*SSQ(volumes[1]));
            const float PSNR = 10 * log10(MSE);
            log(MSE, PSNR);
        }
    }
    bool mouseEvent(const Image& target, int2 cursor, int2 size, Event event, Button button) override {
        if((button == WheelUp &&  currentIndex> 0) || (button == WheelDown && currentIndex < volumes.size-1)) {
            if(button == WheelUp) currentIndex--; else currentIndex++;
            sliceView.volume = current();
            volumeView.setCurrent( current() );
            return true;
        }
        return HBox::mouseEvent(target, cursor, size, event, button);
    }
} app ( arguments() );

#else
#include "thread.h"
#include "cdf.h"
#include "project.h"
#include "time.h"

struct Viewer {
    buffer<Map> files;
    buffer<VolumeF> volumes;
    buffer<Projection> projections = evaluateProjections(volumes[0].sampleCount, int2(256,256), 256); //evaluateProjections(current()->sampleCount, int2(504,378), 5041);*/

    Viewer(ref<string> paths) :
        files( apply(paths,[](string path){ return Map(path); } ) ),
        volumes ( apply(paths.size, [&](uint i){ return VolumeF(parseSize(paths[i]), cast<float>((ref<byte>)files[i])); }) ) {
        ImageF image (projections[0].imageSize);
        for(int _unused t: range(3)) {
            //{Time time; time.start(); for(Projection p: projections) projectGL(image, volumes[0], p); log("GPU", time);}
            {Time time; time.start(); for(Projection p: projections) project(image, volumes[0], p); log("CPU", time);}
        }
        log(paths);
    }
} app ( arguments() );
#endif
