#pragma once
/// \file graphics.h 2D graphics primitives (fill, blit, line)
#include "image.h"
#include "font.h"

/// Primary colors
static constexpr bgr3f black (0, 0, 0);
static constexpr bgr3f red (0, 0, 1);
static constexpr bgr3f green (0, 1, 0);
static constexpr bgr3f blue (1, 0, 0);
static constexpr bgr3f white (1, 1, 1);

/// Fill graphic element
struct Fill {
    vec2 origin, size;
	bgr3f color = black; float opacity = 1;
};

/// Image graphic element
struct Blit {
    vec2 origin, size;
    Image image;
};

/// Text graphic element
struct Glyph {
    vec2 origin;
    Font& font;
	uint index;
	bgr3f color = black;
};

/// Line graphic element
struct Line {
    vec2 a, b;
	bgr3f color;
};

/// Set of graphic elements
struct Graphics {
    array<Fill> fills;
    array<Blit> blits;
    array<Glyph> glyphs;
    array<Line> lines;
    explicit operator bool() const { return fills || blits || glyphs || lines; }
    void append(const Graphics& o, vec2 offset) {
        for(auto e: o.fills) { e.origin += offset; fills.append(e); }
        for(const auto& e: o.blits) blits.append(e.origin+offset, e.size, share(e.image));
        for(auto e: o.glyphs) { e.origin += offset; glyphs.append(e); }
        for(auto e: o.lines) { e.a += offset; e.b += offset; lines.append(e); }
    }
};
inline Graphics copy(const Graphics& o) { return {copy(o.fills), copy(o.blits), copy(o.glyphs), copy(o.lines)}; }

inline String str(const Graphics& o) { return str(o.fills.size, o.blits.size, o.glyphs.size, o.lines.size); }
