#include "data.h"
#include "matrix.h"
#include "process.h"
#include "raster.h"
#include "window.h"
#include "interface.h"
#include "text.h"

inline float acos(float t) { return __builtin_acos(t); }

// Directional light with angular diameter
inline vec3 light(vec3 surfaceNormal, vec3 lightDirection, vec3 lightColor=vec3(1,1,1), float angularDiameter=PI) {
    float t = ::acos(dot(lightDirection,surfaceNormal)); // angle between surface normal and light principal direction
    float a = max<float>(-PI/2,-t-angularDiameter/2); // lower bound of the light integral
    float b = min<float>(PI/2,angularDiameter/2-t); // upper bound of the light integral
    float R = sin(b) - sin(a); // evaluate integral on [a,b] of cos(t-dt)dt (lambert reflectance model) //TODO: Oren-Nayar
    R /= 2*sin(angularDiameter/2); // normalize
    return vec3(R*lightColor);
}

// An Element generated by a Production
struct Module  {
    byte symbol;
    array<float> arguments;
    operator byte() const { return symbol; }
};
string str(const Module& o) { return str(o.symbol)+"("_+str(o.arguments,',')+")"_; }
Module copy(const Module& o){return Module __(o.symbol,copy(o.arguments));}

// Abstract base class to represent expressions
struct Expression {
    virtual float evaluate(ref<float>) const = 0;
    virtual string str() const = 0;
};
string str(const Expression& o) { return o.str(); }

struct Immediate : Expression {
    float value;
    Immediate(float value):value(value){}
    float evaluate(ref<float>) const override { return value; }
    string str() const override { return ftoa(value); }
};
struct Parameter : Expression {
    int index;
    Parameter(int index):index(index){}
    float evaluate(ref<float> a) const override { return a[index]; }
    string str() const override { return "$"_+dec(index); }
};
struct Operator : Expression {
    unique<Expression> left,right;
    Operator(unique<Expression>&& left, unique<Expression>&& right):left(move(left)),right(move(right)){}
    float evaluate(ref<float> a) const override { return left->evaluate(a)*right->evaluate(a); }
    string str() const override { return left->str()+"*"_+right->str(); }
};

struct Production {
    byte symbol;
    array<unique<Expression> > arguments;
    virtual Module operator()(ref<float> parameters) const {
        Module m; m.symbol=symbol;
        for(const unique<Expression>& e: arguments) m.arguments << e->evaluate(parameters);
        return m;
    }
};
string str(const Production& o) { return o.arguments?string(str(o.symbol)+"("_+str(o.arguments,',')+")"_):string(str(o.symbol)); }

struct Rule {
    ref<byte> left; byte edge; ref<byte> right;
    /*Expression condition=Immediate(true);*/ array<Production> productions;
};
string str(const Rule& o) { return /*str(o.left)+"<"_+*/str(o.edge)/*+">"_+str(o.right)*/+" → "_+str(o.productions); }

struct LSystem {
    array<Rule> rules;
    array<Module> axiom;
    //ref<byte> ignore;

    unique<Expression> parse(const map<ref<byte>,float>& constants, const array<ref<byte> >& parameters,
                             unique<Expression>&& e, TextData& s) {
        char c = s.peek();
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')) {
            ref<byte> identifier = s.identifier();
            int i = parameters.indexOf(identifier);
            if(i>=0) return unique<Parameter>(i);
            else return unique<Immediate>(constants.at(identifier));
        } else if(c>='0'&&c<='9') { return unique<Immediate>(s.decimal());
        } else if(c=='*') { s.next(); return unique<Operator>(move(e),parse(constants,parameters,move(e),s));
        } else error(s.untilEnd());
    }

    LSystem(){}
    LSystem(ref<byte> source) {
        map<ref<byte>,float> constants;
        for(ref<byte> line: split(source,'\n')) {
            TextData s(line); if(line.contains(':')) s.until(':'); s.skip();
            if(!s) continue;
            if(line.contains('=')) {
                auto name = s.until('=');
                constants.insert(name) = toDecimal(s.untilEnd());
            } else if(find(line,"→"_)) {
                Rule rule;
                rule.edge = s.next();
                array<ref<byte>> parameters;
                if(s.match('(')) while(!s.match(')')) { parameters << s.identifier(); s.match(','); assert(s); }
                s.skip();
                s.match("→"_);
                s.skip();
                while(s) {
                    Production p;
                    p.symbol = s.next();
                    if(s.match('(')) while(!s.match(')')) {
                        unique<Expression> e;
                        while(!s.match(',') && s.peek()!=')') {
                            e = parse(constants,parameters,move(e),s);
                            s.skip();
                            assert(s);
                        }
                        p.arguments << move(e);
                    }
                    rule.productions << move(p);
                }
                rules << move(rule);
            } else {
                assert(!axiom);
                for(;s;) {
                    Module module;
                    module.symbol = s.next();
                    if(s.match('(')) while(!s.match(')')) {
                        module.arguments << s.decimal();
                        s.skip();
                        if(!s.match(',') && s.peek()!=')') error(s.untilEnd());
                    }
                    axiom << move(module);
                }
            }
        }
    }

    array<Module> generate(int level) const {
        array<Module> code = copy(axiom);
        for(int unused i: range(level)) {
            array<Module> next;
            for(uint i: range(code.size())) { const Module& c = code[i];
                array<array<Module>> matches; //bool sensitive=false;
                for(const Rule& r: rules) {
                    if(r.edge!=c.symbol) continue;
                    if(r.left || r.right) {
                        array<float> arguments;
                        if(r.left) {
                            array<byte> path;
                            for(int j=i-1; path.size()<r.left.size && j>=0; j--) {
                                const Module& c = code[j];
                                if(c==']') { while(code[j]!='[') j--; continue; } // skip brackets
                                if(c=='+' || c=='-' || c=='[' /*|| ignore.contains(c)*/) continue;
                                path << c;
                                arguments << c.arguments;
                            }
                            if(path!=r.left) continue;
                        }
                        arguments << c.arguments;
                        if(r.right) {
                            array<byte> path;
                            for(uint j=i+1; path.size()<r.right.size && j<code.size(); j++) {
                                const Module& c = code[j];
                                if(c=='[') { while(code[j]!=']') j++; continue; } // skip brackets
                                if(c=='+' || c=='-' /*|| ignore.contains(c)*/) continue;
                                path << c;
                                arguments << c.arguments;
                            }
                            if(path!=r.right) continue;
                        }
                        //if(!r.condition(arguments)) continue;
                        //if(!sensitive) matches.clear(), sensitive=true;
                        array<Module> modules;
                        for(const Production& production: r.productions) modules << production(c.arguments);
                        matches << modules;
                    } else {
                        //if(sensitive) continue;
                        //if(!r.condition(c.arguments)) continue;
                        array<Module> modules;
                        for(const Production& production: r.productions) modules << production(c.arguments);
                        matches << modules;
                        //}
                    }
                }
                    assert(matches.size()<=1);
                    //if(matches) next << copy(matches[random()%matches.size()]);
                    if(matches) next << copy(matches[0]);
                    else next << c;
            }
            code = move(next);
        }
        return code;
    }
};

/// Bracketed, Stochastic, context-sensitive, parametric L-system
struct Editor : Widget {
    Folder folder = Folder(""_,cwd());
    Bar<Text> systems;

    Window window __(this,int2(0,0),"L-System Editor"_);
    LSystem system;
    uint current=0, level=6; bool label=false;

    struct Line { vec3 a,b; float wa,wb; };
    array<Line> lines;
    vec3 sceneMin=0, sceneMax=0;

    void openSystem(uint index) {
        if(level>10) level=10;
        ref<byte> name = systems[index].text;
        system = LSystem(readFile(string(name+".l"_),folder));
        generate();
    }
    void generate() {
        lines.clear(); sceneMin=0, sceneMax=0;

        // Turtle interpretation of modules string generated by an L-system
        array<mat4> stack; array<float> lineWidthStack;
        mat4 state; float previousLineWidth=1,lineWidth=1;
        for(const Module& module : system.generate(level)) { char symbol=module.symbol;
            float a = module.arguments?module.arguments[0]*PI/180:0;
            if(symbol=='\\'||symbol=='/') state.rotateX(symbol=='\\'?a:-a);
            else if(symbol=='&'||symbol=='^') state.rotateY(symbol=='&'?a:-a);
            else if(symbol=='-' ||symbol=='+') state.rotateZ(symbol=='+'?a:-a);
            else if(symbol=='!') lineWidth=module.arguments[0];
            else if(symbol=='$') { //set Y horizontal (keeping X), Z=X×Y
                vec3 X; for(int i=0;i<3;i++) X[i]=state(i,0);
                vec3 Y = cross(vec3(1,0,0),X);
                float y = length(Y);
                if(y<0.01) continue; //X is colinear to vertical (all possible Y are already horizontal)
                Y /= y;
                assert(Y.x==0);
                vec3 Z = cross(X,Y);
                for(int i=0;i<3;i++) state(i,1)=Y[i], state(i,2)=Z[i];
            }
            else if(symbol=='[') stack << state, lineWidthStack << lineWidth;
            else if(symbol==']') state = stack.pop(), previousLineWidth = lineWidth = lineWidthStack.pop();
            else if(symbol=='f' || symbol=='F') {
                vec3 A = (state*vec3(0,0,0)).xyz();
                state.translate(vec3(module.arguments[0]?:1,0,0)); //forward axis is +X
                vec3 B = (state*vec3(0,0,0)).xyz();
                if(symbol=='F') lines << Line __(A,B,previousLineWidth,lineWidth);
                previousLineWidth=lineWidth;
                sceneMin=min(sceneMin,B);
                sceneMax=max(sceneMax,B);
                // Apply tropism
                vec3 X; for(int i=0;i<3;i++) X[i]=state(i,0);
                vec3 Y = cross(vec3(-1,0,0),X);
                float y = length(Y);
                if(y<0.01) continue; //X is colinear to tropism (all rotations are possible)
                assert(Y.x==0);
                state.rotate(0.22,state.inverse().normalMatrix()*Y);
            }
        }
        window.render();
    }

    Editor() {
        window.localShortcut(Escape).connect(&exit);
        window.backgroundCenter=window.backgroundColor=0xFF;

        array<string> files = folder.list(Files);
        for(string& file : files) if(endsWith(file,".l"_)) systems << string(section(file,'.'));
        systems.activeChanged.connect(this,&Editor::openSystem);
        systems.setActive(2);

        window.localShortcut(Key(KP_Sub)).connect([this]{if(level>0) level--; generate();});
        window.localShortcut(Key(KP_Add)).connect([this]{if(level<256) level++; generate();});
    }

    int2 lastPos;
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override {
        if(systems.mouseEvent(cursor,int2(size.x,16),event,button)) return true;

        int2 delta = cursor-lastPos; lastPos=cursor;
        if(event==Press && button==LeftButton) {

        } else if(event==Motion && button==LeftButton) {
            rotation += vec2(delta)*float(PI)/vec2(size);
        } else if(event==Press && button==WheelDown) {
            scale *= 17.0/16;
        } else if(event==Press && button==WheelUp) {
            scale *= 15.0/16;
        } else return false;
        return true;
    }

    float scale=1;
    vec3 position=0;
    vec2 rotation=0;
    mat4 view() {
        mat4 view;
        view.scale(scale);
        view.translate(position);
        view.rotateX(rotation.y); // pitch
        view.rotateY(rotation.x); // yaw
        view.rotateZ(PI/2); //+X (heading) is up
        return view;
    }

    void render(int2 targetPosition, int2 targetSize) override {
        systems.render(targetPosition,int2(targetSize.x,16));
        Text(string(dec(level)),32).render(int2(targetPosition+int2(16)));

        position=0; scale = 1;
        if(1) { //Fit window
            mat4 view; view.rotateZ(PI/2); //+X (heading) is up
            vec2 m=min(view*sceneMin,view*sceneMax).xy(), M=max(view*sceneMin,view*sceneMax).xy();
            vec2 size = M-m;
            scale = min(targetSize.x/size.x,targetSize.y/size.y)*0.5;
            vec2 margin = vec2(targetSize)/scale-size;
            position = vec3(vec2(vec2(targetSize)/scale/2.f).x,vec2(-m+margin/2.f).y,0);
        } else { //Fixed size
            scale = 16;
            position = vec3(targetSize.x/2,0,0);
        }

        { // Render
            mat4 view = this->view();
            vec3 sky = normalize(view.normalMatrix()*vec3(1,0,0));
            vec3 sun = normalize(view.normalMatrix()*vec3(1,1,0));
            Rasterizer raster __(framebuffer.width,framebuffer.height);
            raster.clear();
            for(Line line: lines) {
                // project end points
                vec4 A=view*line.a; float wa=line.wa*scale;
                vec4 B=view*line.b; float wb=line.wb*scale;
                vec3 X = normalize(B.xyz()-A.xyz()), Y=cross(vec3(0,0,1),X), Z=cross(X,Y);
                // compute line equation to interpolate [-1,1] across cylinder
                float a=-X.y, b=X.x, c= a*A.x + b*A.y;
                float l = sqrt(a*a+b*b)*max(wa,wb)/2; a/=l, b/=l; c/=l;

                function<vec4(float,float)> shader = [&view,a,b,c,Y,Z,sky,sun](float x, float y){
                    float d = ::clip(-1.f,a*x+b*y-c,1.f);
                    vec3 N = d*Y + sqrt(1-d*d)*Z;

                    vec3 diffuseLight =
                            light(N,sky,vec3(1./2,1./4,1./8)) +
                            light(N,sun,vec3(1./2,3./4,7./8), PI/2);

                    vec3 albedo = vec3(1,1,1);
                    vec3 diffuse = albedo*diffuseLight;
                    return vec4(diffuse,1.f);
                };
                raster.circle(A,(wa-1)/2,shader);
                raster.line(A,B,wa,wb,shader);
            }
            raster.resolve(targetPosition,targetSize);
        }
    }
} application;
