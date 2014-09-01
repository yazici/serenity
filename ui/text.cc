#include "text.h"
#include "graphics.h"
#include "font.h"
#include "utf8.h"

struct NameSize { String name; float size; };
bool operator ==(const NameSize& a, const NameSize& b) { return a.name == b.name && a.size == b.size; }
string str(const NameSize& x) { return str(x.name, x.size); }

/// Returns a font, loading from disk and caching as needed
Font* getFont(string fontName, float size, ref<string> fontTypes, bool hint) {
    String key = fontName+fontTypes[0]+(hint?"H"_:""_);
    assert_(!key.contains(' '));
    static map<NameSize,unique<Font>> fonts; // Font cache
    unique<Font>* font = fonts.find(NameSize{copy(key), size});
    return font ? font->pointer
                : fonts.insert(NameSize{copy(key),size},unique<Font>(Map(findFont(fontName, fontTypes), fontFolder()), size, key, hint)).pointer;
}

/// Layouts formatted text with wrapping, justification and links
struct TextLayout {
    struct Glyph : Font::Metrics, ::Glyph {
        Glyph(const Font::Metrics& metrics, const ::Glyph& glyph) : Font::Metrics(metrics), ::Glyph(glyph) {}
    };
    float width(const ref<Glyph>& word) {
        float max=0;
        for(const Glyph& glyph : word) if(glyph.code!=',' && glyph.code!='.') max=::max(max, glyph.origin.x - glyph.bearing.x + glyph.width);
        return max;
    }
    /*float bbWidth(const ref<Glyph>& word) {
        float min=inf, max=-inf;
        for(const Glyph& glyph : word) {
            min=::min(min, glyph.origin.x - glyph.bearing.x);
            max=::max(max, glyph.origin.x - glyph.bearing.x + glyph.width);
        }
        return max-min;
    }*/
    float min(const ref<Glyph>& word) {
        assert_(word);
        float min=inf; for(const Glyph& glyph : word) min=::min(min, glyph.origin.x - glyph.bearing.x); return min;
    }
    float max(const ref<Glyph>& word) {
        assert_(word);
        float max=-inf; for(const Glyph& glyph : word) max=::max(max, glyph.origin.x - glyph.bearing.x + glyph.width); return max;
    }
    float advance(const ref<Glyph>& word) {
        assert_(word);
        float max=-inf; for(const Glyph& glyph : word) max=::max(max, glyph.origin.x + glyph.advance); return max;
    }

    // Parameters
    float size;
    float wrap;
    float interline;
    float spaceAdvance;

    // Variables
    float lineOriginY = 0;
    array<array<Glyph>> words;
    vec2 bbMax = 0;

    // Outputs
    array<array<array<Glyph>>> glyphs;
    array<Line> lines;

    void nextLine(bool justify) {
        if(words) {
            float length=0; for(const ref<Glyph>& word: words) length += advance(word); // Sums word lengths
            if(words.last()) length += -advance(words.last()) + width(words.last()); // For last word of line, use last glyph width instead of advance
            float space = (justify && words.size>1) ? (wrap-length)/(words.size-1) : spaceAdvance;

            // Layouts
            float x=0; // Line pen
            auto& line = glyphs.append();
            for(const ref<Glyph>& word: words) {
                auto& wordOut = line.append();
                for(Glyph glyph: word) {
                    glyph.origin += vec2(x, lineOriginY);
                    bbMax = ::max(bbMax, glyph.origin-glyph.bearing+glyph.size);
                    wordOut.append( glyph );
                }
                x += advance(word);
                x += space;
            }
            words.clear();
        }
        lineOriginY += interline*size;
    }

    void nextWord(array<Glyph>&& word, bool justify) {
        float length = 0;
        for(const ref<Glyph>& word: words) length += advance(word) + spaceAdvance;
        length += width(word); // Last word
        if(wrap && length > wrap && words) nextLine(justify); // Would not fit
        assert_(word);
        words << move(word); // Adds to current line (might be first of a new line)
    }

    TextLayout(const ref<uint>& text, float size, float wrap, string fontName, bool hint, float interline, bool justify)
        : size(size), wrap(wrap), interline(interline) {
        // Fraction lines
        struct Context {
            TextFormat format; float fontSize; vec2 origin; size_t start; array<Context> children; float maxX; vec2 position; size_t end;
            //mref<Glyph> word(const mref<Glyph>& word) const { return word(children[0].start, children[0].end); }
            void translate(/*const mref<Glyph>& word, */vec2 offset) {
                origin += offset; position += offset; maxX += offset.x;
                for(auto& e: children) e.translate(offset);
                //for(auto& e: this->word(word)) e.origin.x += offset;
            }
        };
        struct Line { size_t line, word; size_t start, split, end; };
        array<Line> lines;
        {
            Font* font = getFont(fontName, size, {""_,"R"_,"Regular"_}, hint);
            uint16 spaceIndex = font->index(' ');
            spaceAdvance = font->metrics(spaceIndex).advance;
            float xHeight = font->metrics(font->index('x')).height;

            lineOriginY = interline*font->ascender;

            // Glyph positions
            array<Glyph> word;

            // Format context
            array<Context> stack;
            TextFormat format = Regular;
            float fontSize = size;
            vec2 origin = 0;
            size_t start = 0;
            array<Context> children;
            vec2 position = 0;
            float maxX = 0;

            // Toggle format
            bool bold=false, italic=false;
            // Kerning
            uint16 previous=spaceIndex;
            int previousRightOffset = 0; // Hinted kerning

            uint i=0;
            for(; i<text.size; i++) {
                uint c = text[i];
                /***/ if(c==' ') position.x += spaceAdvance;
                else if(c=='\t') position.x += 4*spaceAdvance; //FIXME: align
                else break;
            }
            for(; i<text.size; i++) {
                uint c = text[i];

                if(c==' '||c=='\t'||c=='\n') { // Next word/line
                    previous = spaceIndex;
                    if(!stack) {
                        if(word) nextWord(move(word), justify);
                        position.x = 0;
                        if(c=='\t') position.x += 4*spaceAdvance; //FIXME: align
                        if(c=='\n') nextLine(false);
                    } else {
                        assert_(c==' ');
                        position.x += spaceAdvance;
                    }
                    continue;
                }

                if(c<LastTextFormat) { //00-1F format control flags (bold,italic,underline,strike,link)
                    assert_(c!='\t' && c!='\n');
                    if(c<End) { // Push context
                        /*if((c==Numerator && previousFormat==Denominator) || (c==Denominator))
                            split = word.size; // Center*/
                        //if(c==Numerator || c==Regular) start = word.size;

                        maxX = ::max(position.x, maxX);
                        stack << Context{format, fontSize, origin, start, move(children), maxX, position, word.size};

                        format = TextFormat(c);
                        if(format==Subscript || format==Superscript) fontSize *= 2./3;

                        /*TextFormat previousFormat = children ? children.last().format : TextFormat::Regular;
                        if((format==Superscript && previousFormat==Subscript) || (format==Subscript && previousFormat==Superscript))
                            position.x = previousPosition.x; // Stack
                        if((format==Numerator && previousFormat==Denominator) || (format==Denominator))
                            position.x = previousPosition.x; // Stack*/

                        start = word.size;
                        origin = position;
                    }
                    else if(c==End) { // Pop context
                        /*if(format == Stack || format == Fraction) {
                            assert_(children.size==2 && children[0].end == children[1].start, children.size);
                            mref<Glyph> word0 = word(children[0].start, children[0].end);
                            mref<Glyph> word1 = word(children[1].start, children[1].end);
                            assert_(word0 && word1, word.size, word0.size, word1.size);
                            float offset = min(word0)-min(word1) + ((max(word0)-min(word0))-(max(word1)-min(word1)))/2;
                            if(offset > 0) { children[1].translate(vec2( offset,0)); for(auto& e: word1) e.origin.x += offset; }
                            else              { children[0].translate(vec2(-offset,0)); for(auto& e: word0) e.origin.x -= offset; }
                        }*/
                        if(format == Fraction) {
                            assert_(children.size==2 && children[0].end == children[1].start, children.size);
                            lines << Line{glyphs.size, words.size, children[0].start, children[0].end, children[1].end};
                        }
                        maxX = ::max(position.x, maxX);
                        Context child = {format, fontSize, origin, start, move(children), maxX, position, word.size};
                        Context context = stack.pop();
                        format = context.format;
                        fontSize = context.fontSize;
                        origin = context.origin;
                        start = context.start;
                        children = move(context.children);
                        children << move(child);
                        assert_(context.position.x <= context.maxX);
                        //maxX = context.maxX; //::max(maxX, context.maxX);
                        position.y = context.position.y;
                        if(format == Stack || format == Fraction) {
                            //maxX = context.maxX;
                            assert_(children.size<=2);
                            if(children.size==1) position.x = context.position.x;
                        } else {
                            //maxX = ::max(maxX, context.maxX);
                            //position.x = maxX;
                        }
                        assert_(position.x <= maxX);
                    }
                    else if(c==Bold) bold=!bold;
                    else if(c==Italic) italic=!italic;
                    else error(c);

                    if(bold && italic) font = getFont(fontName, fontSize, {"BoldItalic"_,"RBI"_,"Bold Italic"_}, hint);
                    else if(bold) font = getFont(fontName, fontSize, {"Bold"_,"RB"_}, hint);
                    else if(italic) font = getFont(fontName, fontSize, {"Italic"_,"I"_,"Oblique"_}, hint);
                    else font = getFont(fontName, fontSize, {""_,"R"_,"Regular"_}, hint);

                    if(c==Superscript) position.y -= fontSize/2;
                    if(c==Subscript) position.y += font->ascender/2;
                    if(c==Numerator) position.y += -xHeight - (-font->descender)*(1+2./3+4./9);
                    if(c==Denominator) position.y += -xHeight + font->ascender +  -(font->descender)*(2./3+4./9) /*VAlign*/;

                    continue;
                }

                uint16 index = font->index(c);

                // Kerning
                if(previous != spaceIndex) position.x += font->kerning(previous, index);
                previous = index;

                Font::Metrics metrics = font->metrics(index);

                if(hint) { // Hinted kerning
                    if(previousRightOffset - metrics.leftOffset >= 32) position.x -= 1;
                    if(previousRightOffset - metrics.leftOffset < -32 ) position.x += 1;
                    previousRightOffset = metrics.rightOffset;
                }

                if(c != ' ' && c != 0xA0) {
                    vec2 offset = 0;
                    if(c==toUCS4("⌊"_)[0] || c==toUCS4("⌋"_)[0]) offset.y += fontSize/3; // Fixes too high floor signs from FreeSerif
                    assert_(metrics.size, hex(c));
                    word << Glyph(metrics,::Glyph{position+offset, *font, c});
                }
                position.x += metrics.advance;
            }
            if(word) nextWord(move(word), false);
            nextLine(false);
            bbMax.y = ::max(bbMax.y, lineOriginY - interline*size /*Reverts last line space*/ + interline*(-font->descender)); // inter widget spacing
        }

        for(auto& line: lines) {
            const auto& word = glyphs[line.line][line.word];
            assert_(line.start < line.split && line.split < line.end && line.end <= word.size, line.start, line.split, line.end, word.size, toUTF8(text));
            const auto& fract = word(line.start, line.end);
            const auto& num = word(line.start, line.split);
            const auto& den = word(line.split,line.end);
            float numMaxY = ::max(apply(num, [](const Glyph& g) { return g.origin.y - g.bearing.y + g.height; }));
            float denMinY = ::min(apply(den, [](const Glyph& g) { return g.origin.y - g.bearing.y; }));
            float midY = (numMaxY+denMinY) / 2;
            float minX  = ::min(apply(fract, [](const Glyph& g) { return g.origin.x - g.bearing.x; }));
            float maxX  = ::max(apply(fract, [](const Glyph& g) { return g.origin.x - g.bearing.x + g.width; }));
            this->lines << ::Line{vec2(minX, midY), vec2(maxX, midY)};
        }
    }
};

Text::Text(const string& text, float size, vec3 color, float alpha, float wrap, string font, bool hint, float interline, bool center)
    : text(toUCS4(text)), size(size), color(color), alpha(alpha), wrap(wrap), font(font), hint(hint), interline(interline), center(center) {}

TextLayout Text::layout(float wrap) const {
    if(center) {
        TextLayout layout(text, size, wrap, font, hint, interline, false); // Layouts without justification
        //assert_(layout.bbMin >= vec2(-2,0));
        wrap = layout.bbMax.x;
        assert_(wrap >= 0, wrap);
    }
    return TextLayout(text, size, wrap, font, hint, interline, true);
}

int2 Text::sizeHint(int2 size) const {
    vec2 textSize = layout(size.x ? min<float>(wrap, size.x) : wrap).bbMax;
    return max(minimalSizeHint, int2(textSize));
}

Graphics Text::graphics(int2 size) const {
    TextLayout layout = this->layout(min<float>(wrap, size.x));
    //assert_(layout.bbMin >= vec2(-2,0), layout.bbMin);
    vec2 textSize = layout.bbMax;

    Graphics graphics;
    vec2 offset = max(vec2(0), vec2(center ? (size.x-textSize.x)/2.f : 0, (size.y-textSize.y)/2.f));
    for(const auto& line: layout.glyphs) for(const auto& word: line) for(Glyph e: word) { e.origin += offset; graphics.glyphs << e; }
    for(auto e: layout.lines) { e.a += offset; e.b +=offset; graphics.lines << e; }
    return graphics;
}
