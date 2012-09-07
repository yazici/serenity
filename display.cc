#include "display.h"

/// Clip
array<Rect> clipStack;
Rect currentClip=Rect(0);

/// Render
Image framebuffer;

void fill(Rect rect, byte4 color) { //TODO: blend
    rect = rect & currentClip;
    for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        byte4& d = framebuffer(x,y);
        d = byte4((int4(d)*(255-color.a) + int4(color)*color.a)/255);
    }
}

void blit(int2 target, const Image& source, uint8 opacity) {
    Rect rect = (target+Rect(source.size())) & currentClip;
    if(source.alpha) {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 s = source(x-target.x,y-target.y); int a=s.a*opacity/255;
            byte4& d = framebuffer(x,y);
            byte4 t = byte4((int4(d)*(255-a) + int4(s)*a)/255); t.a=min(255,d.a+a);
            d = t;
        }
    } else {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            framebuffer(x,y) = source(x-target.x,y-target.y);
        }
    }
}

void multiply(int2 target, const Image& source, uint8 opacity) {
    Rect rect = (target+Rect(source.size())) & currentClip;
    for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        byte4 s = source(x-target.x,y-target.y); int a=s.a*opacity/255;
        byte4& d = framebuffer(x,y);
        byte4 t = byte4((int4(d)*int4(s))/255); t.a=min(255,d.a+a);
        d = t;
    }
}
