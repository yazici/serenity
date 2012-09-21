#pragma once
#include "string.h"
#include "vector.h"
#include "matrix.h"
#include "map.h"
#include "font.h"
#include "interface.h"

struct PDF : Widget {
    void open(const ref<byte>& path, const Folder& folder);
    int2 sizeHint() override;
    void render(int2 position, int2 size) override;

    Map file;
    mat32 Tm,Cm;

    float x1,y1,x2,y2;
    void extend(vec2 p) { if(p.x<x1) x1=p.x; if(p.x>x2) x2=p.x; if(p.y<y1) y1=p.y; if(p.y>y2) y2=p.y; }

    void drawPath(array<array<vec2> >& paths, int flags);
    enum Flags { Close=1,Stroke=2,Fill=4,OddEven=8,Winding=16,Trace=32 };
    struct Line { vec2 a,b; };
    array<Line> lines;

    void drawText(const ref<byte>& font, int fontSize, float spacing, float wordSpacing, const ref<byte>& data);
    map<ref<byte>, string> fontNames;
    map<ref<byte>, Font> fonts;
    map<ref<byte>, array<float> > widths;
    struct Character { string fontName; float size; uint16 index; vec2 pos; uint16 code; };
    array<Character> characters;

    signal<int, vec2, float,const ref<byte>&, int> onGlyph;
    signal<const ref<vec2>&> onPath;
    array< array<vec2> > paths;
};
