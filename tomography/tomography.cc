#include "cg.h"
#include "plot.h"
#include "window.h"
#include "layout.h"
#include "graphics.h"
#include "view.h"
#include "sum.h"

struct Application : Poll {
    // Evaluation
    Thread thread {19};
    const VolumeF projectionData;
    const VolumeF referenceVolume;
    const float SSQ = ::SSQ(referenceVolume);
    ConjugateGradient reconstruction {referenceVolume.size, projectionData};

    // Interface
    int upsample = 4;
    Value projectionIndex;
    SliceView projectionView {&projectionData, upsample, projectionIndex};
    VolumeView reconstructionView {&reconstruction.x, projectionData.size, upsample, projectionIndex};
    HBox top {{&projectionView, &reconstructionView}};
    Plot plot;
    Value sliceIndex;
    SliceView referenceSliceView {&referenceVolume, upsample, sliceIndex};
    SliceView reconstructionSliceView {&reconstruction.x, upsample, sliceIndex};
    HBox bottom {{&plot, &referenceSliceView, &reconstructionSliceView}};
    VBox layout {{&top, &bottom}};
    Window window {&layout, strx(projectionData.size)+" "_+strx(referenceVolume.size) , int2(3*projectionView.sizeHint().x,projectionView.sizeHint().y+512)}; // FIXME

    Application(string projectionPath, string referencePath) : Poll(0,0,thread), projectionData(Map(projectionPath)),referenceVolume(Map(referencePath)) { queue(); thread.spawn(); }
    void event() {
        reconstruction.step();
        const float PSNR = 10*log10( ::SSE(referenceVolume, reconstruction.x) / SSQ );
        plot["CG"_].insert(reconstruction.totalTime.toFloat(), -PSNR);
        log("\t", reconstruction.totalTime.toFloat(), -PSNR);
        reconstructionView.render();
        reconstructionSliceView.render();
        fill(plot.target, Rect(plot.target.size()), white); //FIXME: partial Window::renderBackground
        plot.render();
        window.immediateUpdate();
        queue();
    }
} app ("data/"_+arguments()[0]+".proj"_, "data/"_+arguments()[0]+".ref"_);
