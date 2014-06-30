#include "thread.h"
#include "variant.h"
#include "text.h"
#include "window.h"
#include "graphics.h"

struct Application : Widget {
    Folder results = "Results"_;
    array<String> names = results.list(); // Kept for references
    array<string> parameters = split("Size,Grain Radius,Resolution,Trajectory,Rotations,Photons,Projections,Subsets size"_,',');
    array<array<Variant>> values;
    map<array<string>, float/*Variant*/> points;
    float min = inf, max = -inf;

    Window window {this, "Results"_};
    Application() {
        values.grow(parameters.size);
        values[parameters.indexOf("Trajectory"_)] << Variant(String("single"_)) << Variant(String("double"_)) << Variant(String("adaptive"_)); // Force this specific order
        for(string name: names) {
            if(name.contains('.')) continue;
            array<string> arguments = split(name);
            assert_(arguments.size == parameters.size, parameters, arguments);
            for(uint i: range(parameters.size)) if(!values[i].contains(arguments[i])) values[i].insertSorted(Variant(String(arguments[i])));
            float bestTotalNMSE = inf;
            for(TextData s = readFile(name, results);s;) {
                uint _unused k = s.integer(); s.skip(" "_);
                float _unused centerNMSE = s.decimal(); s.skip(" "_);
                float _unused extremeNMSE = s.decimal(); s.skip(" "_);
                float totalNMSE = s.decimal(); s.skip(" "_);
                float _unused SNR = s.decimal(); s.skip("\n"_);
                bestTotalNMSE = ::min(bestTotalNMSE, totalNMSE);
            }
            float value = bestTotalNMSE;
            min = ::min(min, value), max = ::max(max, value);
            points.insert(move(arguments), value);
        }
        window.setSize(-1);
        window.show();
    }
    array<string> dimensions[2] = {split("Trajectory Photons"_),split("Rotations Projections"_)};
    /// Returns number of cells for \a axis at \a level
    int cellCount(uint axis, uint level=0) {
        int cellCount = 1;
        for(string parameter: dimensions[axis].slice(level)) {
            assert_(parameters.contains(parameter));
            cellCount *= values[parameters.indexOf(parameter)].size;
        }
        return cellCount;
    }
    int2 cellCount() { return int2(cellCount(0),cellCount(1)); }
    int2 levelCount() { return int2(dimensions[0].size,dimensions[1].size); }
    int2 sizeHint() { return (levelCount().yx()+int2(1)+cellCount()) * int2(80); }
    void render() override {
        assert_(cellCount(), cellCount());
        int2 cellSize = target.size() / (levelCount().yx()+int2(1)+cellCount());
        const vec3 textColor = black;
        // Fixed parameters in unused top-left corner
        String fixed;
        for(uint i: range(parameters.size)) if(values[i].size==1) fixed << parameters[i]+": "_+str(values[i])+"\n"_;
        Text(fixed).render(clip(target, Rect(int2(dimensions[1].size,dimensions[0].size)*cellSize)));
        // Content
        for(uint X: range(cellCount(0))) {
            for(uint Y: range(cellCount(1))) {
                map<string, string> arguments; // Argument values corresponding to current cell
                for(int axis: range(2)) {
                    uint x = ref<uint>({X,Y})[axis];
                    for(uint level: range(dimensions[axis].size)) {
                        string parameter = dimensions[axis][level];
                        uint index = x / cellCount(axis, level+1);
                        arguments[parameter] = values[parameters.indexOf(parameter)][index];
                        x = x % cellCount(axis, level+1);
                    }
                }
                array<string> key;
                for(uint i: range(parameters.size)) {
                    string parameter = parameters[i];
                    if(arguments.contains(parameter)) key << arguments[parameter];
                    else {
                        assert_(values[i].size == 1, "Multiple values for",parameter,":",values[i]);
                        key << values[i][0];
                    }
                }
                if(points.contains(key)) {
                    Image cell = clip(target, ((levelCount().yx()+int2(1)+int2(X,Y)) * cellSize)+Rect(cellSize));
                    float value = points.at(key);
                    float v = (value-min)/(max-min);
                    fill(cell, Rect(cell.size()), vec3(0,1-v,v));
                    Text((value==min?format(TextFormat(Bold|Italic|Underline)):""_)+str(value),16+16*(1-v),textColor).render(cell);
                }
            }
        }
        // Headers (and lines over fills)
        for(uint axis: range(2)) {
            uint X = 1;
            for(uint level: range(dimensions[axis].size)) {
                int2 origin = int2(dimensions[!axis].size-1+1, level);
                if(axis) origin=origin.yx();
                Text(dimensions[axis][level],16,textColor).render(clip(target, (origin*cellSize)+Rect(cellSize)));

                uint parameter = parameters.indexOf(dimensions[axis][level]);
                uint size = values[parameter].size;
                for(uint x: range(X)) {
                    for(uint index: range(size)) {
                        int2 origin = int2(dimensions[!axis].size +1 + x*size + index*cellCount(axis,level+1), level);
                        int2 size = int2(cellCount(axis,level+1), 1);
                        if(axis) origin=origin.yx(), size=size.yx();
                        origin *= cellSize;
                        if(level<dimensions[axis].size-1) {
                            if(!axis) fill(target, Rect(origin,int2(origin.x+1, target.size().y)));
                            if(axis) fill(target, Rect(origin,int2(target.size().x,origin.y+1)));
                        }
                        Text(values[parameter][index],16,textColor).render(clip(target, origin+Rect(size*cellSize)));
                    }
                }
                X *= size;
            }
        }
    }
} app;