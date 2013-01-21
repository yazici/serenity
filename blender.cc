#include "blender.h"
#include "process.h"
#include "string.h"
#include "data.h"
#include "gl.h"
#include "window.h"
#include "map.h"
SHADER(blender)

/// Used to fix pointers in .blend file-block sections
struct Block {
    uint64 begin; // First memory address used by pointers pointing in this block
    uint64 end; // Last memory address used by pointers pointing in this block
    int64 delta; // Target memory address where the block was loaded - begin
};
bool operator<(const Block& a, const Block& b) { return a.begin<b.begin; }

/// SDNA type definitions
struct Struct {
    ref<byte> name;
    uint size;
    struct Field {
        ref<byte> typeName;
        uint reference;
        ref<byte> name;
        uint count;
        const Struct* type;
    };
    array<Field> fields;
};
string str(const Struct::Field& f) { return " "_+f.typeName+repeat('*',f.reference)+" "_+f.name+(f.count!=1?"["_+str(f.count)+"]"_:string())+";"_; }
string str(const Struct& s) { return "struct "_+s.name+" {\n"_+str(s.fields,'\n')+"\n};"_; }

/// Parses a .blend file
struct BlendView : Widget {
    // View
    int2 lastPos; // last cursor position to compute relative mouse movements
    vec2 rotation=vec2(PI/3,-PI/3); // current view angles (yaw,pitch)
    Window window __(this, int2(1050,1050), "BlendView"_, Window::OpenGL);

    // Renderer
    GLFrameBuffer framebuffer;
    GLShader shadow = GLShader(blender,"transform"_);
    GLShader atmosphere = GLShader(blender,"atmosphere"_);
    GLShader resolve = GLShader(blender,"screen resolve"_);

    // File
    Map mmap; // Keep file mmaped
    const Scene* scene=0; // Blender scene (root handle to access all data)

    // Scene
    vec3 worldMin=0, worldMax=0; // Scene bounding box in world space
    vec3 worldCenter=0; float worldRadius=0; // Scene bounding sphere in world space

    // Light
    vec3 lightMin=0, lightMax=0; // Scene bounding box in light space
    const float sunYaw = PI/3;
    const float sunPitch = 3*PI/4;
    mat4 sun; // sun light transform
    GLFrameBuffer sunShadow;

    // Shaders
    map<string, GLShader> shaders;

    // Geometry
    struct Vertex {
        vec3 position; // World-space position
        vec3 normal; // World-space vertex normals
        vec3 color; // BGR albedo (TODO: texture mapping)
    };
    struct Object {
        GLBuffer buffer;
        GLShader& shader;
    };
    array<Object> objects;
    //TODO: instances = {object, mat4[]}

    BlendView()
        : mmap("Island/Island.blend", home(), Map::Read|Map::Write) { // with write access to fix pointers
        load();
        parse();

        window.localShortcut(Escape).connect(&::exit);
    }

    /// Recursively fix all pointers in an SDNA structure
    void fix(const array<Block>& blocks, const Struct& type, const ref<byte>& buffer) {
        BinaryData data (buffer);
        for(const Struct::Field& field: type.fields) {
            if(!field.reference) {
                if(field.type->fields) for(uint i unused: range(field.count)) fix(blocks, *field.type, data.Data::read(field.type->size));
                else data.advance(field.count*field.type->size);
            } else for(uint i unused: range(field.count)) {
                uint64& pointer = (uint64&)data.read<uint64>();
                if(!pointer) continue;
                for(const Block& block: blocks.slice(blocks.binarySearch( Block __(pointer) )-1)) {
                    if(pointer >= block.begin && pointer < block.end) {
                        pointer += block.delta;
                        if(field.reference>1) { // pointer arrays
                            assert(pointer==block.begin+block.delta);
                            assert((block.end-block.begin)%8==0);
                            uint64* array = (uint64*)pointer;
                            uint size = (block.end-block.begin)/8;
                            for(uint i : range(size)) {
                                uint64& pointer = array[i];
                                if(!pointer) continue;
                                for(const Block& block: blocks.slice(blocks.binarySearch( Block __(pointer) )-1)) {
                                    if(pointer >= block.begin && pointer < block.end) {
                                        pointer += block.delta;
                                        goto found2;
                                    }
                                }
                                error("not found");
                                pointer = 0;
                                found2:;
                            }
                        }
                        goto found;
                    }
                }
                pointer = 0;
                found:;
            }
        }
    }

    /// Parses SDNA to fix all pointers
    void load() {
        array<Block> blocks; // List of file blocks containing pointers to be fixed
        array<Struct> structs; // SDNA structure definitions

        BinaryData file(mmap);
        //Assumes BLENDER-v262 (64bit little endian)
        file.seek(12);
        blocks.reserve(32768);
        while(file) { // Parses SDNA
            const BlockHeader& header = file.read<BlockHeader>();
            ref<byte> identifier(header.identifier,4);
            BinaryData data( file.Data::read(header.size) );
            blocks << Block __(header.address, header.address+header.size, int64(uint64(data.buffer.buffer.data)-header.address));
            if(identifier == "DNA1"_) {
                data.advance(4); //SDNA
                data.advance(4); //NAME
                uint nameCount = data.read();
                array< ref<byte> > names;
                for(uint unused i: range(nameCount)) names << data.untilNull();
                data.align(4);
                data.advance(4); //TYPE
                uint typeCount = data.read();
                array< ref<byte> > types;
                for(uint unused i: range(typeCount)) types << data.untilNull();
                data.align(4);
                data.advance(4); //TLEN
                ref<uint16> lengths = data.read<uint16>(typeCount);
                data.align(4);
                data.advance(4); //STRC
                uint structCount = data.read();
                for(uint unused i: range(structCount)) {
                    Struct s;
                    uint16 index = data.read();
                    ref<byte> name = types[index];
                    uint size = lengths[index];
                    uint16 fieldCount = data.read();
                    array<Struct::Field> fields;
                    for(uint unused i: range(fieldCount)) {
                        ref<byte> type = types[(uint16)data.read()];
                        ref<byte> name = names[(uint16)data.read()];
                        uint reference=0, count=1;
                        TextData s (name);
                        if(s.match("(*"_)) { //parse function pointers
                            name.data+=2; name.size-=2+3;
                            type = "function"_; reference++;
                        } else {
                            while(s.match('*')) { //parse references
                                name.data++; name.size--;
                                reference++;
                            }
                        }
                        s.whileNot('['); if(s.match('[')) { //parse static arrays
                            name.size = s.index;
                            count = s.integer();
                            s.match(']');
                            while(s.match('[')) { // Flatten multiple indices
                                count *= s.integer();
                                s.match(']');
                            }
                        }
                        fields << Struct::Field __(type, reference, name, count, 0);
                    }
                    structs << Struct __(name, size, move(fields));
                }
                structs << Struct __("char"_,1) << Struct __("short"_,2) << Struct __("int"_,4) << Struct __("uint64_t"_,8)
                        << Struct __("float"_,4) << Struct __("double"_,8);
                for(Struct& s: structs) {
                    for(Struct::Field& f: s.fields) {
                        for(const Struct& match: structs) if(match.name == f.typeName) { f.type = &match; break; }
                        assert(f.type || f.reference, f);
                    }
                }
            }
            if(identifier == "ENDB"_) break;
        }
        quicksort(blocks);

        file.seek(12);
        while(file) { // Fixes pointers
            const BlockHeader& header = file.read<BlockHeader>();
            ref<byte> identifier(header.identifier,4);
            BinaryData data (file.Data::read(header.size));

            //if(identifier == "DATA"_) continue; // dynamic arrays, pointer arrays are fixed when parsing the reference
            if(identifier == "SC\0\0"_) scene = (Scene*)data.buffer.buffer.data;
            if(identifier == "DNA1"_) continue;
            if(identifier == "ENDB"_) break;

            const Struct& type = structs[header.type];
            //assert(header.size >= header.count*type.size);
            if(header.size >= header.count*type.size)
                if(type.fields) for(uint unused i: range(header.count)) fix(blocks, type, data.Data::read(type.size));
        }

        //for(const Struct& match: structs) if(match.name == "Object"_) log(match);
    }

    /// Extracts relevant data from Blender structures
    void parse() {
        sun=mat4(); sun.rotateX(sunPitch); sun.rotateZ(sunYaw);

        for(const Base& base: scene->base) {
            if(base.object->type==::Object::Mesh) {
                const Mesh* mesh = base.object->data;

                array<Vertex> vertices;
                for(const MVert& vert: ref<MVert>(mesh->mvert, mesh->totvert)) {
                    vec3 position = vec3(vert.co);
                    vec3 normal = normalize(vec3(vert.no[0],vert.no[1],vert.no[2]));
                    vec3 color = vec3(1,1,1); //TODO: MCol

                    vertices << Vertex __(position, normal, color);
                }

                ref<MLoop> loops(mesh->mloop, mesh->totloop);
                array<uint> indices;
                uint materialCount=1;
                for(const MPoly& poly: ref<MPoly>(mesh->mpoly, mesh->totpoly)) {
                    materialCount = max(materialCount, uint(poly.mat_nr+1));
                    assert(poly.totloop==3 || poly.totloop==4);
                    uint a=loops[poly.loopstart].v, b=loops[poly.loopstart+1].v;
                    for(uint i: range(poly.loopstart+2, poly.loopstart+poly.totloop)) {
                        uint c = loops[i].v;
                        indices << a << b << c;
                        b = c;
                    }
                }

                for(Vertex& vertex: vertices) {
                    // Normalizes smoothed normals (weighted by triangle areas)
                    vertex.normal=normalize(vertex.normal);

                    // Computes scene bounds in world space to fit view
                    worldMin=min(worldMin, vertex.position);
                    worldMax=max(worldMax, vertex.position);

                    // Compute scene bounds in light space to fit shadow
                    vec3 P = (sun*vertex.position).xyz();
                    lightMin=min(lightMin,P);
                    lightMax=max(lightMax,P);
                }

                // Submits geometry
                GLBuffer buffer;
                buffer.upload<Vertex>(vertices);
                buffer.upload(indices);

                // Compiles shader
                static Folder shaderFolder("shaders",cwd());

                ref<byte> vert="default"_, frag="default"_;
                if(base.object->data->totcol>=1 && base.object->data->mat[0]) {
                    string name =  toLower(str((const char*)base.object->data->mat[0]->id.name+2));
                    if(existsFile(string(name+".vert"_),shaderFolder)) vert=name;
                    if(existsFile(string(name+".frag"_),shaderFolder)) frag=name;
                    log(name, vert, frag);
                }
                if(!shaders.contains(vert+frag))
                    shaders.insert(vert+frag, GLShader(
                                       readFile(string(vert+".vert"_),shaderFolder),
                                       readFile(string(frag+".frag"_),shaderFolder)) );
                objects << Object __(move(buffer), shaders.at(vert+frag));

                //log(base.object->id.name, mesh->id.name, vertices.size(), indices.size());
            }
        }

        //FIXME: compute smallest enclosing sphere
        worldCenter = (worldMin+worldMax)/2.f; worldRadius=length(worldMax.xy()-worldMin.xy())/2.f;
    }

    // Orbital view control
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override {
        int2 delta = cursor-lastPos; lastPos=cursor;
        if(event==Motion && button==LeftButton) {
            rotation += float(2.f*PI)*vec2(delta)/vec2(size); //TODO: warp
            rotation.y= clip(float(-PI/2),rotation.y,float(0)); // Keep pitch between [-PI/2,0]
        }
        else if(event == Press) focus = this;
        else return false;
        return true;
    }

    void render(int2 unused position, int2 unused size) override {
        uint width=size.x, height = size.y;
        // Computes projection transform
        mat4 projection;
        projection.perspective(PI/4,width,height,1./4,4);
        // Computes view transform
        mat4 view;
        view.scale(1.f/worldRadius); // fit scene (isometric approximation)
        view.translate(vec3(0,0,-2*worldRadius)); // step back
        view.rotateX(rotation.y); // pitch
        view.rotateZ(rotation.x); // yaw
        view.translate(vec3(0,0,-worldCenter.z));
        // View-space lighting
        mat3 normalMatrix = view.normalMatrix();
        vec3 sunLightDirection = normalize((view*sun.inverse()).normalMatrix()*vec3(0,0,-1));
        vec3 skyLightDirection = normalize(normalMatrix*vec3(0,0,1));

        // Render sun shadow map
        if(!sunShadow)
            sunShadow = GLFrameBuffer(GLTexture(4096,4096,GLTexture::Depth24|GLTexture::Shadow|GLTexture::Bilinear|GLTexture::Clamp));
        sunShadow.bind(true);
        glDepthTest(true);
        glCullFace(true);

        // Normalizes to -1,1
        sun=mat4();
        sun.translate(-1);
        sun.scale(2.f/(lightMax-lightMin));
        sun.translate(-lightMin);
        sun.rotateX(sunPitch);
        sun.rotateZ(sunYaw);

        shadow.bind();
        shadow["modelViewProjectionTransform"] = sun;
        for(Object& object: objects) {
            object.buffer.bindAttribute(shadow, "position", 3, __builtin_offsetof(Vertex,position));
            object.buffer.draw();
        }

        // Normalizes to xy to 0,1 and z to -1,1
        sun=mat4();
        sun.translate(vec3(0,0,0));
        sun.scale(vec3(1.f/(lightMax.x-lightMin.x),1.f/(lightMax.y-lightMin.y),1.f/(lightMax.z-lightMin.z)));
        sun.translate(-lightMin);
        sun.rotateX(sunPitch);
        sun.rotateZ(sunYaw);

        if(framebuffer.width != width || framebuffer.height != height) framebuffer=GLFrameBuffer(width,height);
        framebuffer.bind(true);
        glBlend(false);

        for(Object& object: objects) {
            GLShader& shader = object.shader;
            shader.bind();
            shader["modelViewProjectionTransform"] = projection*view;
            shader["normalMatrix"] = normalMatrix;
            shader["sunLightTransform"] = sun;
            shader["sunLightDirection"] = sunLightDirection;
            shader["skyLightDirection"] = skyLightDirection;
            shader["shadowScale"] = 1.f/sunShadow.depthTexture.width;
            shader.bindSamplers("shadowMap"); GLTexture::bindSamplers(sunShadow.depthTexture);
            object.buffer.bindAttribute(shader,"position",3,__builtin_offsetof(Vertex,position));
            object.buffer.bindAttribute(shader,"color",3,__builtin_offsetof(Vertex,color));
            object.buffer.bindAttribute(shader,"normal",3,__builtin_offsetof(Vertex,normal));
            object.buffer.draw();
        }

        //TODO: fog
        atmosphere["inverseProjectionMatrix"] = projection.inverse();
        atmosphere["sunLightDirection"] = -sunLightDirection;
        glDrawRectangle(atmosphere);

        GLTexture color(width,height,GLTexture::RGB16F);
        framebuffer.blit(color);

        GLFrameBuffer::bindWindow(int2(position.x,window.size.y-height-position.y), size);
        glDepthTest(false);
        glCullFace(false);

        resolve.bindSamplers("framebuffer"); GLTexture::bindSamplers(color);
        glDrawRectangle(resolve);

        GLFrameBuffer::bindWindow(0, window.size);
    }

} application;
