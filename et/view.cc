#include "view.h"
#include "scene.h"
#include "object.h"
#include "shader.h"
#include "file.h"
#include "gl.h"

View::View(Scene& scene) : scene(scene) {
    vec4 position_yaw = scene.defaultPosition();
    position = position_yaw.xyz(), yaw=position_yaw.w;
}

void View::render(int2, int2 size) {
    int w = size.x, h = size.y;

    // Updates view
    view=mat4(); view.rotateX(-pitch); view.rotateZ(-yaw); // World to view coordinates transform (i.e rotates world in the reverse direction)
    // Assumes constant frame rate (60fps)
    velocity += view.inverse()*vec3(strafe*sprint,0,-walk*sprint)+vec3(0,0,jump*sprint);
    velocity *= 31.0/32; // Velocity damping (friction)
    position += velocity;
    view.translate( -position );
    projection=mat4(); projection.perspective(PI/4, w, h, 1, 32768); inverseProjection=projection.inverse();
    // Computes view frustum planes
    mat4 m = projection*view;
    planes[0] = vec4( m(3,0) + m(0,0), m(3,1) + m(0,1), m(3,2) + m(0,2), m(3,3) + m(0,3) );
    planes[1] = vec4( m(3,0) - m(0,0), m(3,1) - m(0,1), m(3,2) - m(0,2), m(3,3) - m(0,3) );
    planes[2] = vec4( m(3,0) - m(1,0), m(3,1) - m(1,1), m(3,2) - m(1,2), m(3,3) - m(1,3) );
    planes[3] = vec4( m(3,0) + m(1,0), m(3,1) + m(1,1), m(3,2) + m(1,2), m(3,3) + m(1,3) );
    planes[4] = vec4( m(2,0), m(2,1), m(2,2), m(2,3) );
    planes[5] = vec4( m(3,0) - m(2,0), m(3,1) - m(2,1), m(3,2) - m(2,2), m(3,3) - m(2,3) );
    for(int i=0;i<6;i++) { planes[i]=normalize(planes[i]); signs[i]=sign(planes[i].xyz()); }

    if(frameBuffer.depthTexture.size() != size) frameBuffer = GLFrameBuffer(GLTexture(w,h,Depth24), GLTexture(w,h, sRGB8));
    frameBuffer.bind(ClearDepth|ClearColor, vec4(scene.backgroundColor,1.f));

    render(/*surfaceRender, lightRender, true*/);

    GLFrameBuffer::bindWindow(0, size, 0);
    {GLShader& program = gamma.bind();
        program.bindSamplers({"tex"_}); program.bindFragments({"color"_});
        frameBuffer.colorTexture.bind(0);
        glDrawRectangle(program);
    }

    if( norm(velocity) > 0.1 ) contentChanged(); // Keeps rendering at 60fps
}

void View::render(/*GLFrameBuffer& deferRender,GLFrameBuffer& targetRender, bool withShadow, bool reverseWinding*/) {
    // Draws opaque and alpha tested objects into G-Buffer
#if 1
    //deferRender.bind(true);
    glDepthTest(true);
    if(scene.opaque) { glCullFace(true); draw(scene.opaque); glCullFace(false); }
#endif
    // Draws lights using G-Buffer
#if 0
    DepthTest=false;
    targetRender.bind(true); deferRender.bindSamplers();
    glBlendAdd();
    static GLShader program; if(!program.id) program.compile(getGLSL(QStringList("screen deferred position light"),"vertex"),
                                                             getGLSL(QStringList("screen deferred position light"),"fragment")
                                                             .replace("%",QString::number(1)));
    program.bind();
    program.bindSamplers("depthBuffer","albedoBuffer","normalBuffer");
    program.bindFragments("color");
    float nearPlane=(inverseProjection*vec3(0,0,-1)).z, farPlane=(inverseProjection*vec3(0,0,1)).z;
    program["A"]= farPlane/(nearPlane-farPlane); program["B"]= farPlane*nearPlane/(nearPlane-farPlane);
    program["fogOpacity"]=scene.sky?scene.sky->fogOpacity:8192;
    program["inverseProjectionMatrix"]=inverseProjection;

    for(int n=0;n<scene.lights.count();n++) { Light* light = &scene.lights[n]; //OPTI: query visible
        // View frustum culling
        int shortcut=light.planeIndex;
        if( dot(light.origin, planes[shortcut].xyz()) <= -planes[shortcut].w-light.radius ) goto cull;
        for(int i=0;i<6;i++) if( dot(light.origin, planes[i].xyz())+planes[i].w <= -light.radius ) {
            light.planeIndex=i;
            goto cull;
        }
        {
            light.project(projection,view);
            vec4 lightPosition[1]; vec3 lightColor[1];
            lightPosition[0]=vec4(light.center.x,light.center.y,light.center.z,light.radius);
            program["lightPosition"].set(lightPosition,1);
            lightColor[0]=light.color;
            program["lightColor"].set(lightColor,1);
            light.clipMin.z=(light.clipMin.z+1)/2;
            light.clipMax.z=(light.clipMax.z+1)/2;
            renderQuad(light.clipMin,light.clipMax);
        }
        cull: ;
    }
#endif
    // Forward render transparent objects
    glDepthTest(true);  //TODO: forward lighting
    if(scene.blendAlpha) {
      //PolygonOffsetFill=true; glPolygonOffset(-2,-1);
        glBlendAlpha(); draw(scene.blendAlpha,BackToFront);
      //PolygonOffsetFill=false;
    }
    if(scene.blendColor) {
      //PolygonOffsetFill=true; glPolygonOffset(-2,-1);
        glBlendColor(); draw(scene.blendColor,BackToFront);
      //PolygonOffsetFill=false;
    }
    glDepthTest(false); glBlendNone();
}

void View::draw(map<GLShader*, array<Object>>& objects, Sort /*sort*/) {
    for(pair<GLShader*, array<Object>> e: objects) {
        GLShader& program = *e.key;
        program.bind();
        //shader["fogOpacity"] = /*scene.sky ? scene.sky->fogOpacity :*/ 8192;
        program.bindFragments({"color"_});

        array<Object>& objects = e.value;
        mat4 currentTransform=mat4(0); vec3 currentColor=0; mat3x2 tcMods[4]={0,0,0,0}; vec3 rgbGens[4]={0,0,0,0}; // Save current state to minimize state changes (TODO: UBOs)
#ifdef VIEW_FRUSTUM_CULLING
        QMap<float,Object*> depthSort; //useless without proper partitionning
        for(int n=0;n<objects.count();n++) { Object* object = objects[n];
            int shortcut=object.planeIndex;
            if( dot(object.center+object.extent*signs[shortcut], planes[shortcut].xyz())+planes[shortcut].w < 0 ) goto cull;
            for(int i=0;i<6;i++) {
                if( dot(object.center+object.extent*signs[i], planes[i].xyz())+planes[i].w < 0 ) {
                    object.planeIndex=i; goto cull;
                }
            }
            depthSort.insertMulti( (sort==FrontToBack?-1:+1)*(view*object.center).z, objects[n] );
            cull: ;
        }
        foreach(Object* object,depthSort.values()) {
#else
        for(Object& object: objects) {
#endif
            Shader& shader = object.surface.shader;

            if(shader.skyBox) { object.transform = mat4(); object.transform.translate(position); } // Clouds move with view
            if(object.transform != currentTransform) {
                program["modelViewProjectionMatrix"_] = projection*view*object.transform;
                program["normalMatrix"_]= (view*object.transform).normalMatrix();
                GLUniform modelLightMatrix=program["modelLightMatrix"_]; // Model to world
                if(modelLightMatrix) {
                    mat4 light; light.scale(vec3(1)/(scene.gridMax-scene.gridMin)); light.translate(-scene.gridMin);
                    modelLightMatrix = light*object.transform;
                    program["viewNormalMatrix"_] = view.normalMatrix(); // World to view normal matrix
                }
                currentTransform=object.transform;
            }
            if(object.uniformColor!=currentColor) { GLUniform uniformColor = program["uniformColor"_]; if(uniformColor) uniformColor=object.uniformColor; currentColor=object.uniformColor; }
            int sampler=0; for(int i: range(shader.size)) {
                Texture& tex = shader[i];
                if(tex.texture) {
                    assert_(tex.texture);
                    program["tex"_+str(i)] = sampler;
                    assert_(i<4);
                    if(tcMods[i]!=tex.tcMod) { GLUniform uniform = program["tcMod"_+str(i)]; if(uniform) uniform=tex.tcMod; tcMods[i]=tex.tcMod; }
                    if(rgbGens[i]!=tex.rgbGen) { GLUniform uniform = program["rgbGen"_+str(i)]; if(uniform) uniform=tex.rgbGen; rgbGens[i]=tex.rgbGen; }
                    tex.texture->bind(sampler);
                    sampler++;
                }
            }
            if(find(shader.type,"lightgrid"_)) {
                program["lightGrid0"_]=sampler; scene.lightGrid[0].bind(sampler); sampler++;
                program["lightGrid1"_]=sampler; scene.lightGrid[1].bind(sampler); sampler++;
                program["lightGrid2"_]=sampler; scene.lightGrid[2].bind(sampler); sampler++;
            }

            object.surface.draw(program);
        }
    }
}

bool View::keyPress(Key key, Modifiers) {
    /**/ if(key=='w') walk++;
    else if(key=='a') strafe--;
    else if(key=='s') walk--;
    else if(key=='d') strafe++;
    else if(key==ControlKey) jump--;
    else if(key==' ') jump++;
    else if(key==ShiftKey) sprint = 8;
    else if(key=='q') velocity=vec3(0,0,0);
    else return false;
    return true; // Starts rendering (if view was still)
}
bool View::keyRelease(Key key, Modifiers) {
    if(key=='w') walk--;
    if(key=='a') strafe++;
    if(key=='s') walk++;
    if(key=='d') strafe--;
    if(key==ControlKey) jump++;
    if(key==' ') jump--;
    if(key==ShiftKey) sprint = 2;
    return false;
}
bool View::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    setFocus(this);
    if(event==Press && button==LeftButton) { dragStart=cursor; deltaStart=vec2(yaw, pitch); }
    if(event==Release && button==LeftButton && dragStart==cursor) {
        mat4 transform; transform.perspective(PI/4, size.x, size.y, 1, 32768);
        transform.rotateX(-pitch); transform.rotateZ(-yaw); //transform.translate(-position);*/

        vec3 direction = transform.inverse() * normalize(vec3(2.0*cursor.x/size.x-1,1-2.0*cursor.y/size.y,1));
        float minZ=65536; Object* hit=0;
        for(Object* object: scene.objects) {
            mat4 toObject = object->transform.inverse();
            if(object->surface.intersect(toObject*position,toObject.normalMatrix()*direction,minZ)) hit=object;
        }
        if(selected) selected->uniformColor=vec3(1,1,1); // Deselect previous
        if(hit) {
            selected=hit;
            selected->uniformColor=vec3(1,0.5,0.5);
            log(selected->surface.shader.name);
            log(selected->surface.shader.source);
            log(selected->surface.shader.program->source);
        }
        return true;
    }
    if(event==Motion && button==LeftButton) {
        setDrag(this);
        vec2 delta = deltaStart - float(PI/size.x)*vec2(cursor-dragStart);
        yaw = delta.x; pitch = clip<float>(0, delta.y, PI);
        return true;
    }
    return false;
}
