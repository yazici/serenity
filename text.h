#pragma once
#include "widget.h"
#include "function.h"
#include "image.h"

/// Rich text format control code encoded in 00-1F range
///// \note Strike|Bold (\t) and Strike|Italic (\n) cannot be used
/// \note Mono|Bold (\t) and Mono|Italic (\n) cannot be used
/// \note first word (until ' ') after a Link tag is not displayed but used as \a linkActivated identifier.
enum Format { Regular=0,Bold=1,Italic=2,Underline=4,/*Strike=8,*/Mono=8,Link=16 };
inline string format(Format f) { assert(f<32); string s; s << (char)f; return s; }
inline Format format(uint f) { assert(f<32); return Format(f); }

/// Text is a \a Widget displaying text (can be multiple lines)
struct Text : Widget {
    /// Create a caption that display \a text using a \a size pt (points) font
    Text(string&& text=string(), int size=16, uint8 opacity=255, uint wrap=0);

    void setText(string&& text) { this->text=move(text); textSize=0; }
    void setSize(int size) { this->size=size; textSize=0; }

    /// Displayed text
    string text;
    /// Font size
    int size;
    /// Opacity
    uint8 opacity;
    /// Line wrap limit in pixels (0: no wrap)
    uint wrap=0;
    /// User clicked on this Text
    signal<> textClicked;
    /// User clicked on a \a Format::Link
    signal<const ref<byte>&> linkActivated;

    int2 sizeHint();
    void layout();
    void render(int2 position, int2 size) override;
    bool mouseEvent(int2 cursor, int2 size, Event event, MouseButton button) override;

    // Layout bounding box
    int2 textSize=0;
    // Minimal size hint
    int2 minSize=0;

    // Characters to render
    struct Character { int2 pos; Image image; };
    array<Character> characters;
    // Underlines and strikes
    struct Line { int2 min,max; };
    array<Line> lines;

    // Inline links
    struct Link { uint begin,end; string identifier;};
    array<Link> links;
};

/// TextInput is an editable \a Text
//TODO: multiline
struct TextInput : Text {
    uint cursor=0;

    bool mouseEvent(int2 cursor, int2 size, Event event, MouseButton button) override;
    bool keyPress(Key key) override;
    void render(int2 position, int2 size) override;
};
