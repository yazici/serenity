#include "light.h"
#include "scene.h"
#include "parallel.h"

#include "interface.h"
#include "text.h"
#include "render.h"
#include "window.h"

#include "analyze.h"

struct ViewControl : virtual Widget {
    vec2 viewYawPitch = vec2(0, 0); // Current view angles

    struct {
        vec2 cursor;
        vec2 viewYawPitch;
    } dragStart {0, 0};

    // Orbital ("turntable") view control
    virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
        if(event == Press) {
            dragStart = {cursor, viewYawPitch};
            return true;
        }
        if(event==Motion && button==LeftButton) {
            viewYawPitch = dragStart.viewYawPitch + float(2*PI) * (cursor - dragStart.cursor) / size;
            viewYawPitch.x = clamp<float>(-PI/3, viewYawPitch.x, PI/3);
            viewYawPitch.y = clamp<float>(-PI/3, viewYawPitch.y, PI/3);
        }
        else return false;
        return true;
    }
};

struct LightFieldViewApp : LightField {
    Scene scene;
    Scene::Renderer<0> Zrenderer {scene};
    Scene::Renderer<3> BGRrenderer {scene};

    bool orthographic = false;
    bool sample = true;
    bool raycast = true;
    bool depthCorrect = true;

    struct LightFieldViewWidget : ViewControl, ImageView {
        LightFieldViewApp& _this;
        LightFieldViewWidget(LightFieldViewApp& _this) : _this(_this) {}

        virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& widget) override {
            return ViewControl::mouseEvent(cursor,size,event,button,widget);
        }
        virtual vec2 sizeHint(vec2) override { return vec2(1024); }
        virtual shared<Graphics> graphics(vec2 size) override {
            this->image = _this.render(uint2(size));
            return ImageView::graphics(size);
        }
    } view {*this};
    unique<Window> window = ::window(&view);

    LightFieldViewApp() {
        analyze();
        window->actions[Key('s')] = [this]{ sample=!sample; window->render(); };
        window->actions[Key('r')] = [this]{ raycast=!raycast; window->render(); };
        window->actions[Key('o')] = [this]{ orthographic=!orthographic; window->render(); };
        window->actions[Key('d')] = [this]{ depthCorrect=!depthCorrect; window->render(); };
    }
    Image render(uint2 targetSize) {
        Image target (targetSize);

        mat4 M;
        if(orthographic) {
            M.rotateX(view.viewYawPitch.y); // Pitch
            M.rotateY(view.viewYawPitch.x); // Yaw
            M.scale(vec3(1,1,-1)); // Z-
        } else {
            // Sheared perspective (rectification)
            const float s = (view.viewYawPitch.x+PI/3)/(2*PI/3), t = (view.viewYawPitch.y+PI/3)/(2*PI/3);
            M = shearedPerspective(s, t);
        }

        if(raycast) {
            assert_(imageSize.x == imageSize.y && imageCount.x == imageCount.y);

            ImageH Z (target.size);
            if(depthCorrect) scene.render(Zrenderer, M, {}, Z);

            parallel_chunk(target.size.y*target.size.x, [this, &target, M, &Z](uint, size_t start, size_t sizeI) {
                const int targetStride = target.size.x;
                const mat4 Mi = M.inverse();
                const uint2 imageCount = this->imageCount;
                const uint2 imageSize = this->imageSize;
                const float scale = (float) imageSize.x / imageCount.x; // st -> uv
                const half* fieldZ = this->fieldZ.data;
                const half* fieldB = this->fieldB.data;
                const half* fieldG = this->fieldG.data;
                const half* fieldR = this->fieldR.data;
                const int size1 = this->size1;
                const int size2 = this->size2;
                const int size3 = this->size3;
                assert_(imageSize.x%2==0); // Gather 32bit / half
                const v2si sample2D = {    0,           size1/2};
                const v8si sample4D = {    0,           size1/2,         size2/2,       (size2+size1)/2,
                                     size3/2,   (size3+size1)/2, (size3+size2)/2, (size3+size2+size1)/2};
                for(size_t targetIndex: range(start, start+sizeI)) {
                    int targetX = targetIndex%targetStride, targetY = targetIndex/targetStride;
                    const vec3 O = Mi * vec3(2.f*targetX/float(targetStride-1)-1, 2.f*targetY/float(target.size.y-1)-1, -1);
                    const vec3 P = Mi * vec3(2.f*targetX/float(targetStride-1)-1, 2.f*targetY/float(target.size.y-1)-1, +1);
                    const vec3 d = normalize(P-O);

                    const vec3 n (0,0,1);
                    const float nd = dot(n, d);
                    const vec3 n_nd = n / nd;

                    const vec2 Pst = O.xy() + dot(n_nd, vec3(0,0,1)-O) * d.xy();
                    const vec2 ST = (Pst+vec2(1))/2.f;
                    const vec2 Puv = O.xy() + dot(n_nd, vec3(0,0,0)-O) * d.xy();
                    const vec2 UV = (Puv+vec2(1))/2.f;

                    const vec2 st = vec2(0x1p-16) + vec2(1-0x1p-16) * ST * vec2(imageCount-uint2(1));
                    const vec2 uv = vec2(1-0x1p-16) * UV * vec2(imageSize-uint2(1));

                    if(st[0] < -0 || st[1] < -0) { target[targetIndex]=byte4(0xFF,0,0,0xFF); continue; }
                    const int sIndex = st[0], tIndex = st[1];
                    if(sIndex >= int(imageCount.x)-1 || tIndex >= int(imageCount.y)-1) { target[targetIndex]=byte4(0,0xFF,0xFF,0xFF); continue; }

                    bgr3f S = 0;
                    if(depthCorrect) {
                        const float Zw = Z(targetX, targetY);
                        const float z = Zw-1.f/2;

                        const v4sf x = {st[1], st[0]}; // ts
                        const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                        const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                        v4sf w01st = __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0) // ttTT
                                   * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1); // sSsS

                        v4sf B, G, R;
                        for(int dt: {0,1}) for(int ds: {0,1}) {
                            vec2 uv_ = uv + scale * (fract(st) - vec2(ds, dt)) * (-z) / (z+2);
                            if(uv_[0] < 0 || uv_[1] < 0) { w01st[dt*2+ds] = 0; continue; }
                            int uIndex = uv_[0], vIndex = uv_[1];
                            if(uIndex >= int(imageSize.x)-1 || vIndex >= int(imageSize.y)-1) { w01st[dt*2+ds] = 0; continue; }
                            const size_t base = (size_t)(tIndex+dt)*size3 + (sIndex+ds)*size2 + vIndex*size1 + uIndex;
                            const v2sf x = {uv_[1], uv_[0]}; // vu
                            const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                            const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                            const v4sf Z = toFloat((v4hf)gather((float*)(fieldZ+base), sample2D));
                            const v4sf w01uv = and(__builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0)  // vvVV
                                               * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1) // uUuU
                                               , abs(Z - float4(Zw)) < float4(0x1p-5)); // Discards far samples (tradeoff between edge and anisotropic accuracy)
                            float sum = ::sum(w01uv);
                            const v4sf w01 = float4(1./sum) * w01uv; // Renormalizes uv interpolation (in case of discarded samples)
                            w01st[dt*2+ds] *= sum; // Adjusts weight for st interpolation
                            if(!sum) { B[dt*2+ds] = 0; G[dt*2+ds] = 0; R[dt*2+ds] = 0; continue; }
                            B[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldB+base), sample2D)));
                            G[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldG+base), sample2D)));
                            R[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldR+base), sample2D)));
                        }
                        const v4sf w01 = float4(1./sum(w01st)) * w01st; // Renormalizes st interpolation (in case of discarded samples)
                        const float b = dot(w01, B);
                        const float g = dot(w01, G);
                        const float r = dot(w01, R);
                        S = bgr3f(b, g, r);
                    } else {
                        const int uIndex = uv[0], vIndex = uv[1];
                        if(uv[0] < 0 || uv[1] < 0) { target[targetIndex]=byte4(0,0,0xFF,0xFF); continue; }
                        if(uIndex >= int(imageSize.x)-1 || vIndex >= int(imageSize.y)-1) { target[targetIndex]=byte4(0xFF,0xFF,0,0xFF); continue; }
                        const size_t base = (size_t)tIndex*size3 + sIndex*size2 + vIndex*size1 + uIndex;
                        const v16sf B = toFloat((v16hf)gather((float*)(fieldB+base), sample4D));
                        const v16sf G = toFloat((v16hf)gather((float*)(fieldG+base), sample4D));
                        const v16sf R = toFloat((v16hf)gather((float*)(fieldR+base), sample4D));

                        const v4sf x = {st[1], st[0], uv[1], uv[0]}; // tsvu
                        const v8sf X = __builtin_shufflevector(x, x, 0,1,2,3, 0,1,2,3);
                        static const v8sf _00001111f = {0,0,0,0,1,1,1,1};
                        const v8sf w_1mw = abs(X - floor(X) - _00001111f); // fract(x), 1-fract(x)
                        const v16sf w01 = shuffle(w_1mw, w_1mw, 4,4,4,4,4,4,4,4, 0,0,0,0,0,0,0,0)  // ttttttttTTTTTTTT
                                        * shuffle(w_1mw, w_1mw, 5,5,5,5,1,1,1,1, 5,5,5,5,1,1,1,1)  // ssssSSSSssssSSSS
                                        * shuffle(w_1mw, w_1mw, 6,6,2,2,6,6,2,2, 6,6,2,2,6,6,2,2)  // vvVVvvVVvvVVvvVV
                                        * shuffle(w_1mw, w_1mw, 7,3,7,3,7,3,7,3, 7,3,7,3,7,3,7,3); // uUuUuUuUuUuUuUuU
                        S = bgr3f(dot(w01, B), dot(w01, G), dot(w01, R));
                    }
                    target[targetIndex] = byte4(byte3(float(0xFF)*S), 0xFF);
                }
            });
        } else {
            ImageH B (target.size), G (target.size), R (target.size);
            scene.render(BGRrenderer, M, (float[]){1,1,1}, {}, B, G, R);
            convert(target, B, G, R);
        }
        return target;
    }
} view;
