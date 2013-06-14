/// \file rock.cc Volume data processor
#include "thread.h"
#include "process.h"
#include "volume.h"
#include "volume-operation.h"
#include "time.h"
#include "window.h"
#include "display.h"
#include "render.h"
#include "png.h"

//#include "source.h"
//#include "capsule.h"
//#include "average.h"
//#include "median.h"
//#include "histogram.h"
//#include "threshold.h"
//#include "distance.h"
//#include "skeleton.h"
//#include "floodfill.h"
//#include "rasterize.h"
//#include "kernel-density-estimation.h"
//#include "export.h"

struct GraphProcess : virtual Process {
    string dot(array<const Rule*>& once, const ref<byte>& output) {
        const Rule& rule = ruleForOutput(output);
        string s;
        if(!once.contains(&rule)) {
            once << &rule;
            s <<'"'<<hex(ptr(&rule))<<'"'<< "[shape=record, label=\""_<<rule.operation;
            for(ref<byte> output: rule.outputs) s<<"|<"_<<output<<"> "_<<output;
            s<<"\"];\n"_;
            for(ref<byte> input: rule.inputs) {
                s<<'"'<<hex(ptr(&ruleForOutput(input)))<<"\":\""_<<input<<"\" -> \""_<<hex(ptr(&rule))<<"\"\n"_;
                s<<dot(once, input);
            };
        }
        return s;
    }

    void generateSVG(const ref<ref<byte>>& targets, const ref<byte>& name, const ref<byte>& folder){
        array<const Rule*> once;
        string s ("digraph \""_+name+"\" {\n"_);
        for(const ref<byte>& target: targets) s << dot(once, target);
        s << "}"_;
        string path = "/dev/shm/"_+name+".dot"_;
        writeFile(path, s);
        ::execute("/ptmp/bin/dot"_,{path,"-Tsvg"_,"-o"_+folder+"/"_+name+".svg"_});
    }
};

/// From an X-ray tomography volume, segments rocks pore space and computes histogram of pore sizes
struct Rock : virtual PersistentProcess, virtual GraphProcess, Widget {
    FILE(rock) // Rock process definition (embedded in binary)i
    Rock(const ref<ref<byte>>& args) {
        specialParameters += "dump"_; specialParameters += "graph"_; specialParameters += "view"_; specialParameters += "cylinder"_;
        string process;
        for(const ref<byte>& arg: args) if(endsWith(arg, ".process"_)) { assert_(!process); process = readFile(arg,cwd); }
        array<ref<byte>> targets = configure(args, process? : rock());
#ifndef BUILD
#define BUILD "undefined"
#endif
        if(arguments.contains("dump"_)) {
            log("Rock binary built on " __DATE__ " " __TIME__ " (" BUILD ")");
            log("Operations:",Interface<Operation>::factories.keys);
            log("Parameters:",parameters());
            log("Results:",resultNames);
            log("Targets:",targets);
            log("Arguments:",arguments, targetsSweeps);
            log("Target paths:",targetPaths);
        }
        if(arguments.contains("graph"_)) { generateSVG(targets, name, getenv("HOME"_)); return; }
        if(!targets) {
            if(arguments || targetsSweeps) log("Arguments:",arguments, targetsSweeps);
            if(targetPaths) log("Target paths:",targetPaths);
            assert_(targets, "Expected target");
        }
        for(uint i: range(targets.size)) execute(targets[i], targetsSweeps[i], arguments);

        if(targetPaths.size>1 || (targetPaths.size==1 && !existsFolder(targetPaths[0],cwd))) { // Copies results to individually named files
            uint resultIndex=0;
            for(uint index: range(targetPaths.size)) {
                if(resultIndex>=targetResults.size) {
                    error("Expected less names, skipped names"_, targetPaths.slice(index),
                          "\nHint: An unknown (mistyped?) target might be interpreted as target path"); //TODO: hint nearest (levenstein distance) target
                    break;
                }
                Time time;
                const ref<byte>& path = targetPaths[index];
                string name, data;
                if(targetsSweeps[index]) { // Concatenates sweep results into a single file
                    const Sweeps& sweeps = targetsSweeps[index];
                    assert_(sweeps.size()==1, "FIXME: Only single sweeps can be concatenated");
                    uint sweepSize = sweeps.values[0].size, dataSize = 0;
                    assert_(sweepSize, targets[index], path, sweeps);
                    for(const shared<Result>& result: targetResults.slice(resultIndex, sweepSize)) {
                        string resultName = result->name+"."_+result->metadata;
                        if(!name) name = copy(resultName);
                        assert_(resultName==name);
                        ref<byte> key = result->relevantArguments.at(sweeps.keys[0]);
                        if(result->metadata=="scalar"_) data << key << "\t"_ << result->data << "\n"_;
                        else {
                            assert_(!data);
                            assert_(!existsFile(path, cwd) || existsFolder(path, cwd), path);
                            Folder folder (path, cwd, true);
                            writeFile(key, result->data, folder);
                            dataSize += result->data.size;
                        }
                    }
                    resultIndex += sweepSize;
                    if(!data) log(name,"->",path, "["_+str(sweepSize)+"x]"_, "["_+str(dataSize/1024/1024)+" MiB]"_, time);
                } else {
                    const shared<Result>& target = targetResults[resultIndex];
                    name = target->name+"."_+target->metadata;
                    data = unsafeReference(target->data);
                    resultIndex++;
                }
                if(data) {
                    if(existsFolder(path, cwd)) {
                        writeFile(name, data, Folder(path, cwd));
                        log(path+"/"_+name, "["_+str(data.size/1024/1024)+" MiB]"_, time);
                    } else {
                        writeFile(path, data, cwd);
                        log(name,"->",path, "["_+str(data.size/1024/1024)+" MiB]"_, time);
                    }
                }
            }
            if(targetResults.slice(resultIndex) && !arguments.contains("view"_)) error("Expected more names, skipped targets"_, targetResults.slice(resultIndex));
        } else if(targetPaths.size == 1) { // Copies results into folder
            const ref<byte>& path = targetPaths[0];
            assert_(!existsFile(path,cwd), "New folder would overwrite existing file", path);
            if(!existsFolder(path,cwd)) Folder(path,cwd,true);
            for(const shared<Result>& target: targetResults) if(target->data.size) {
                Time time;
                writeFile(target->name+"."_+target->metadata, target->data, Folder(path, cwd)), log(path+"/"_+target->name+"."_+target->metadata, time);
            }
        } else assert(arguments.contains("view"_), "Expected target paths"_);

        // Displays target results
        if(arguments.value("view"_,"0"_)!="0"_) {
            for(const shared<Result>& target: targetResults) {
                assert(target->data.size);
                if(target->metadata=="scalar"_) log(target->name, "=", target->data);
                else if(endsWith(target->metadata,"map"_) && count(target->data,'\n')<64) log_(str(target->name, ":\n"_+target->data)); // Map of scalars
                else if(endsWith(target->metadata,".tsv"_) && count(target->data,'\n')<64) log_(str(target->name, ":\n"_+target->data));
                else if(!current && inRange(1u,toVolume(target).sampleSize,4u)) current = share(target); // Displays first displayable volume
            }
        }
        if(current) {
            window = unique<Window>(this,int2(-1,-1),"Rock"_);
            window->localShortcut(PrintScreen).connect(this, &Rock::saveSlice);
            window->localShortcut(Escape).connect([]{exit();});
            window->clearBackground = false;
            updateView();
            window->show();
        }
    }

     void parseSpecialArguments(const ref<ref<byte>>& specialArguments) override {
        for(const ref<byte>& argument: specialArguments) {
            /***/ if(endsWith(argument,".process"_)) {} // Already parsed extern process definition
            else if(existsFolder(argument,cwd) && !Folder(argument,cwd).list(Files|Folders)) remove(Folder(argument,cwd)); // Removes any empty target folder
            else if(!arguments.contains("path"_) && existsFolder(argument,cwd)) arguments.insert("path"_,argument);
            else if(!argument.contains('=')) targetPaths << argument;
            else error("Invalid argument", argument);
        }
        assert_(arguments.contains("path"_), "Usage: rock <source folder containing volume slices> (target name|target path|key=value)*");
        ref<byte> path = arguments.at("path"_);
        name = string(path.contains('/') ? section(path,'/',-2,-1) : path); // Use source path as process name (for storage folder) instead of any first arguments
        PersistentProcess::parseSpecialArguments(specialArguments);
    }

    bool mouseEvent(int2 cursor, int2 size, Event unused event, Button button) {
        if(button==WheelDown||button==WheelUp) {
            int volumeCount=0, volumeIndex=0;
            for(const shared<Result>& target: targetResults) if(toVolume(target)) { if(target==current) volumeIndex=volumeCount; volumeCount++; }
            int newVolumeIndex = clip<int>(0,volumeIndex+(button==WheelUp?1:-1),volumeCount-1);
            if(newVolumeIndex == volumeIndex) return true;
            volumeCount = 0; for(const shared<Result>& target: targetResults) if(toVolume(target)) { if(newVolumeIndex==volumeCount) current=share(target); volumeCount++; }
            updateView();
            return true;
        }
        if(!button) return false;
        /*if(renderVolume) {
            int2 delta = cursor-lastPos;
            lastPos = cursor;
            if(event != Motion) return false;
            rotation += vec2(-2*PI*delta.x/size.x,2*PI*delta.y/size.y);
            rotation.y= clip(float(-PI),rotation.y,float(0)); // Keep pitch between [-PI,0]
        } else*/
        {
            float z = clip(0.f, float(cursor.x)/(size.x-1), 1.f);
            if(sliceZ != z) { sliceZ = z; updateView(); }
        }
        updateView();
        return true;
    }

    void updateView() {
        assert(current);
        Volume volume = toVolume(current);
        int2 size = volume.sampleCount.xy();
        while(2*size<displaySize) size *= 2;
        if(window->size != size) window->setSize(size);
        else window->render();
        window->setTitle(str(current));
    }

    void render(int2 position, int2 size) {
        assert(current);
        Volume volume = toVolume(current);
        if(volume.sampleSize==20) { exit(); return; } // Don't try to display ASCII
        if(volume.sampleSize>4) error(current->name, volume.sampleSize);
        if(renderVolume) {
            mat3 view;
            view.rotateX(rotation.y); // pitch
            view.rotateZ(rotation.x); // yaw
            shared<Result> empty = getResult("empty"_, arguments);
            shared<Result> density = getResult("density"_, arguments);
            shared<Result> intensity = getResult("intensity"_, arguments);
            Time time;
            assert_(position==int2(0) && size == framebuffer.size());
            ::render(framebuffer, toVolume(empty), toVolume(density), toVolume(intensity), view);
#if PROFILE
            log((uint64)time,"ms");
            window->render(); // Force continuous updates (even when nothing changed)
            wait.reset();
#endif
        } else
        {
            Image image = slice(volume, sliceZ, arguments.contains("cylinder"_));
            while(2*image.size()<=size) image=upsample(image);
            blit(position, image);
        }
    }

    void saveSlice() {
        string path = name+"."_+current->name+"."_+current->metadata+".png"_;
        writeFile(path, encodePNG(slice(toVolume(current),sliceZ,arguments.contains("cylinder"_))), home());
        log(path);
    }

    const Folder& cwd = currentWorkingDirectory(); // Reference for relative paths
    array<ref<byte>> targetPaths; // Path to file (or folders) where targets are copied
    shared<Result> current;
    float sliceZ = 1./2; // Normalized z coordinate of the currently shown slice
    unique<Window> window = nullptr;

    bool renderVolume = false;
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
} app ( arguments() );
