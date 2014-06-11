#include "view.h"

Value SliceView::staticIndex;

bool SliceView::mouseEvent(int2 cursor, int2 size, Event, Button button) {
    if(button == WheelDown) { index.value = clip(0u, index.value-1, uint(this->size.z)); index.render(); return true; }
    if(button == WheelUp) { index.value = clip(0u, index.value+1, uint(this->size.z)); index.render(); return true; }
    if(button) { index.value = clip(0, int(cursor.x*(this->size.z-1)/(size.x-1)), int(this->size.z-1)); index.render(); return true; }
    return false;
}

int2 SliceView::sizeHint() { return upsampleFactor * this->size.xy(); }

void SliceView::render() {
    ImageF image = volume ? slice(*volume, index.value) : slice(*clVolume, index.value);
    while(image.size < this->target.size()) image = upsample(image);
    Image target = clip(this->target, (this->target.size()-image.size)/2+Rect(image.size));
    assert_(target.size() == image.size, target.size(), image.size);
    convert(target, image, 0);
}

Value VolumeView::staticIndex;

bool VolumeView::mouseEvent(int2 cursor, int2 size, Event, Button button) {
    if(button) { index.value = clip(0, int(cursor.x*(this->size.z-1)/(size.x-1)), int(this->size.z-1)); index.render(); return true; }
    return false;
}

int2 VolumeView::sizeHint() { return upsampleFactor * size.xy(); }

void VolumeView::render() {
    ImageF image = ImageF( size.xy() );
    project(image, volume, size.z, index.value);
    for(uint _unused i: range(log2(upsampleFactor))) image = upsample(image);
    convert(clip(target, (target.size()-image.size)/2+Rect(image.size)), image, 0);
}
