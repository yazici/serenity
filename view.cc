#include "scene.h"
#include "parallel.h"

#include "interface.h"
#include "text.h"
#include "render.h"
#include "window.h"

inline string basename(string x) {
    string name = x.contains('/') ? section(x,'/',-2,-1) : x;
    string basename = name.contains('.') ? section(name,'.',0,-2) : name;
    assert_(basename);
    return basename;
}

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

struct ViewApp {
    Scene scene {::parseScene(readFile(basename(arguments()[0])+".scene"))};
    //Scene::Renderer<0> Zrenderer {scene};
    Scene::Renderer<3> BGRrenderer {scene};

    struct ViewWidget : ViewControl, ImageView {
        ViewApp& _this;
        ViewWidget(ViewApp& _this) : _this(_this) {}

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

    Image debug {uint2(1024), true};

    ViewApp() {
        // Fits scene
        vec3 min = inff, max = -inff;
        for(Scene::Face f: scene.faces) for(vec3 p: f.position) { min = ::min(min, p); max = ::max(max, p); }
        max.z += 0x1p-8; // Prevents back and far plane from Z-fighting
        const float scale = 2./::max(max.x-min.x, max.y-min.y);
        const float near = scale*(-scene.viewpoint.z+min.z);
        const float far = scale*(-scene.viewpoint.z+max.z);
        //log(min, max, near, far, far/near);
        //log(scale);

        mat4 M;
        // Sheared perspective (rectification)
        const float s = 0, t = 0; //(view.viewYawPitch.x+PI/3)/(2*PI/3), t = (view.viewYawPitch.y+PI/3)/(2*PI/3);
        M = shearedPerspective(s, t, near, far);
        M.scale(scale); // Fits scene within -1, 1
        M.translate(-scene.viewpoint);

        // Fits face UV to maximum projected sample rate
        float maxC = 0;
        vec2 minUV = inff, maxUV = -inff;
        debug.clear(0);
        for(Scene::Face& face: scene.faces) {
            const vec3 a = face.position[0], b = face.position[1], c = face.position[2], d = face.position[3];
            const vec3 O = (a+b+c+d)/4.f;
            const vec3 N = cross(c-a, b-a);
            //assert_(length(cross(cross(b-a, c-a),cross(c-a, d-a)))==0, length(cross(cross(b-a, c-a),cross(c-a, d-a)))); // Planar quad
            //assert_(N.z || (N.x && N.y), N);
            // Viewpoint st with maximum projection
            vec2 st = clamp(vec2(-1), scale*(O.xy()-scene.viewpoint.xy()) + (scale*(O.z-scene.viewpoint.z)/(N.z==0?0/*-0 negates infinities*/:-N.z))*N.xy(), vec2(1));
            //const vec2 uvO = scale*(O.xy()-scene.viewpoint.xy());
            //const vec2 uvO = /*st +*/ scale*(O.z-scene.viewpoint.z)/near * (scale*(O.xy()-scene.viewpoint.xy())/*-st*/);
            const vec2 uvO = (M*O).xy();
            log(a, b, c, d, O, uvO);
            ::render(debug, Text(str(normalize(N)), 32, green, 1, 0, "DejaVuSans", true, 1, 0).graphics(0), (uvO+vec2(1))/2.f*vec2(debug.size));
            if(!N.z) { // FIXME
                log(N, st);
                if(!N.x) st.x = 0;
                if(!N.y) st.y = 0;
            }
            face.color[0] = (st[0]+1)/2;
            face.color[1] = (st[1]+1)/2;
#if 1
            // Projects vertices along st view rays on uv plane (perspective)
            // FIXME
            /*const vec2 uvA = st + scale*(a.z-scene.viewpoint.z)/near * (scale*(a.xy()-scene.viewpoint.xy())-st);
            const vec2 uvB = st + scale*(b.z-scene.viewpoint.z)/near * (scale*(b.xy()-scene.viewpoint.xy())-st);
            const vec2 uvC = st + scale*(c.z-scene.viewpoint.z)/near * (scale*(c.xy()-scene.viewpoint.xy())-st);
            const vec2 uvD = st + scale*(d.z-scene.viewpoint.z)/near * (scale*(d.xy()-scene.viewpoint.xy())-st);*/
            {
                mat4 M;
                M = shearedPerspective(st[0], st[1], near, far);
                M.scale(scale); // Fits scene within -1, 1
                M.translate(-scene.viewpoint);
                const vec2 uvA = (M*a).xy();
                const vec2 uvB = (M*b).xy();
                const vec2 uvC = (M*c).xy();
                const vec2 uvD = (M*d).xy();
                minUV = ::min(minUV, uvA);
                maxUV = ::max(maxUV, uvA);
                minUV = ::min(minUV, uvB);
                maxUV = ::max(maxUV, uvB);
                minUV = ::min(minUV, uvC);
                maxUV = ::max(maxUV, uvC);
                minUV = ::min(minUV, uvD);
                maxUV = ::max(maxUV, uvD);
                const float cellSize = 1./16; // for visualization
#else
            // World space uniform rate parametrization
            const vec2 uvA = a.xy();
            const vec2 uvB = b.xy();
            const vec2 uvC = c.xy();
            const vec2 uvD = d.xy();
            const float cellSize = 16; // for visualization
#endif
            const float maxU = ::max(length(uvB-uvA), length(uvC-uvD)); // Maximum projected edge length along quad's u axis
            const float maxV = ::max(length(uvD-uvA), length(uvC-uvB)); // Maximum projected edge length along quad's v axis
            face.color[0] = maxU;
            face.color[1] = maxV;
            maxC = ::max(maxC, maxU);
            maxC = ::max(maxC, maxV);
            // Scales uv
            for(float& u: face.u) u *= maxU/cellSize;
            for(float& v: face.v) v *= maxV/cellSize;
            for(float& u: face.u) u *= 2;
            for(float& v: face.v) v *= 2;
        }
        }
        log(minUV, maxUV);
        log(maxC);
        for(Scene::Face& face: scene.faces) {
            face.color[0] /= maxC;
            face.color[1] /= maxC;
        }
    }

    Image render(uint2 targetSize) {
        Image target (targetSize);

        // Fits scene
        vec3 min = inff, max = -inff;
        for(Scene::Face f: scene.faces) for(vec3 p: f.position) { min = ::min(min, p); max = ::max(max, p); }
        max.z += 0x1p-8; // Prevents back and far plane from Z-fighting
        const float scale = 2./::max(max.x-min.x, max.y-min.y);
        const float near = scale*(-scene.viewpoint.z+min.z);
        const float far = scale*(-scene.viewpoint.z+max.z);
        //log(min, max, near, far, far/near);

        mat4 M;
        // Sheared perspective (rectification)
        const float s = view.viewYawPitch.x/(PI/3), t = view.viewYawPitch.y/(PI/3);
        M = shearedPerspective(s, t, near, far);
        M.scale(scale); // Fits scene within -1, 1
        M.translate(-scene.viewpoint);

        ImageH B (target.size), G (target.size), R (target.size);
        scene.render(BGRrenderer, M, (float[]){1,1,1}, {}, B, G, R);
        convert(target, B, G, R);
        blit(target, 0, debug);
        window->setTitle(str(s, t));
        return target;
    }
} view;
