#include "text.h"
#include "graphics.h"
#include "font.h"
#include "utf8.h"

struct NameSize { String name; int size; };
bool operator ==(const NameSize& a, const NameSize& b) { return a.name == b.name && a.size == b.size; }
string str(const NameSize& x) { return str(x.name, x.size); }
static map<NameSize,Font> fonts; // Font cache

/// Layouts formatted text with wrapping, justification and links
struct TextLayout {
    float size;
    float wrap;
    float interline;
    float spaceAdvance;
    float penY=0;
    struct Character { Font* font; vec2 pos; uint index; uint width; float advance; uint editIndex; };
    typedef array<Character> Word;
    array<Word> words;
    typedef array<Character> TextLine;
    array<TextLine> text;
    struct Line { Text::Cursor begin,end; };
    array<Line> lines;
    array<Text::Link> links;

    uint lineNumber=0,column=0;
    Text::Cursor current() { return Text::Cursor{lineNumber, column}; }

    uint lastIndex=-1;
    float maxLength = 0;
    void nextLine(bool justify) {
        // Justifies
        float length=0; for(const Word& word: words) { assert_(word,"J"_); length += word.last().pos.x + word.last().advance; } // Sums word lengths
        if(words && words.last()) length += -words.last().last().advance + words.last().last().width; // For last word of line, use last character width instead of advance
        float space=0;
        if(justify && words.size>1) space = (wrap-length)/(words.size-1);
        else space = spaceAdvance;

        // Layouts
        column=0; float penX=0; // Line pen
        lineNumber++; text << TextLine();
        for(uint i: range(words.size)) { Word& word=words[i];
            assert_(word);
            for(Character& c: word) text.last() << Character{c.font, vec2(penX,penY)+c.pos, c.index, 0, c.advance, lastIndex=c.editIndex};
            maxLength = max(maxLength, penX+word.last().pos.x+word.last().width);
            penX += word.last().pos.x + word.last().advance;
            //if(i!=words.size-1) text.last() << Character{0,vec2(penX, penY),0,0,spaceAdvance,lastIndex=lastIndex+1}; // Editable justified space
            penX += space;
        }
        lastIndex++;
        words.clear();
        penY += interline*size;
    }

    Font* getFont(string fontName, int size, string fontType=""_) {
        if(!fonts.contains(NameSize{fontName+fontType, size})) {
            auto font = filter(fontFolder().list(Files|Recursive), [&](string path) {
                    return fontType ? !find(path, fontName+fontType+"."_) && !find(path, fontName+"-"_+fontType+"."_) && !find(path, fontName+"_"_+fontType+"."_)
                                    : !find(path,fontName+"."_); });
            if(!font) return 0;
            assert_(font.size==1, font);
            fonts.insert(NameSize{fontName+fontType,size},Font(File(font.first(), fontFolder()), size));
        }
        return &fonts.at(NameSize{fontName+fontType, size});
    }

    TextLayout(const ref<uint>& text, int size, int wrap, string fontName, float interline, bool justify=false):size(size), wrap(wrap), interline(interline) {
        Font* font = getFont(fontName, size);
        assert_(font, fontName, size);
        uint16 spaceIndex = font->index(' ');
        spaceAdvance = font->advance(spaceIndex); assert(spaceAdvance);
        uint16 previous=spaceIndex;
        bool bold=false, italic=false;//, underline=false;
        Text::Link link;
        Text::Cursor underlineBegin;
        Word word;
        float penX = 0; // Word pen
        penY = interline*font->ascender;
        uint i=0; for(; i<text.size; i++) {
            uint c = text[i];
            /**/ if(c==' ') penX += spaceAdvance;
            else if(c=='\t') penX += 4*spaceAdvance; //FIXME: align
            else break;
        }
        for(; i<text.size; i++) {
            uint c = text[i];
            if(c==' '||c=='\t'||c=='\n' /*c<=' '*/) { // Next word/line
                column++;
                previous = spaceIndex;
                if(word) {
                    if(words) {
                        float length = 0; for(const Word& word: words) { assert_(word,"J",toUTF8(text)); length += word.last().pos.x + word.last().advance + spaceAdvance; }
                        //assert_(length-word.last().advance-spaceAdvance+word.last().width <= wrap, length, length-word.last().advance-spaceAdvance+word.last().width, wrap, words.size);
                        length += word.last().pos.x + word.last().width; // Next word
                        //log("expect", length);
                        if(wrap && length > wrap && words) nextLine(justify); // would not fit
                    }
                    words << move(word); penX = 0; // Add to current line (might be first of a new line)
                }
                if(c=='\n') nextLine(false);
                continue;
            }
            if(c<0x20) { //00-1F format control flags (bold,italic,underline,strike,link)
                if(c==' '||c=='\t'||c=='\n') continue;
                //if(link) { link.end=current(); links << move(link); }
                TextFormat format = ::format(c);
                /**/ if(format==Bold) bold=!bold;
                else if(format==Italic) italic=!italic;
                //else if(format==Underline) { if(underline && current()>underlineBegin) lines << Line{underlineBegin, current()}; }
                else error(int(format));
                /**/ if(bold && italic) font = getFont(fontName, size, "BoldItalic"_) ?: getFont(fontName, size, "Bold Italic"_);
                else if(bold) font = getFont(fontName, size, "Bold"_);
                else if(italic) font = getFont(fontName, size, "Oblique"_) ?: getFont(fontName, size, "Italic"_);
                else font = getFont(fontName, size);
                assert_(font, fontName, bold, italic);
                if(format&Underline) underlineBegin=current();
                if(format&Link) {
                    for(;;) {
                        i++; assert(i<text.size);
                        uint c = text[i];
                        if(c == ' ') break;
                        link.identifier << utf8(c);
                    }
                    link.begin = current();
                }
                continue;
            }
            uint16 index = font->index(c);
            if(previous!=spaceIndex) penX += font->kerning(previous,index);
            previous = index;
            float advance = font->advance(index);
            const Glyph& glyph = font->glyph(index);
            if(glyph.image) { word << Character{font, vec2(penX,0), index, glyph.offset.x+glyph.image.width, advance, i}; column++; }
            penX += advance;
        }
        if(word) {
            float length=0; for(const Word& word: words) if(word) length += word.last().pos.x + word.last().advance + spaceAdvance;
            length += word.last().pos.x + word.last().width; // Last word
            if(wrap && length>wrap) nextLine(justify); // would not fit
            words << move(word); penX = 0; // Adds to current line (might be first of new line)
        }
        nextLine(false); // Clears any remaining words
        penY -= interline*size; // Reverts last line space
        penY += interline*font->ascender; // Adds descender for correct inter widget line spacing
    }
};

Text::Text(const string& text, uint size, vec3 color, float alpha, uint wrap, string font, float interline, bool center)
    : text(toUTF32(text)), size(size), color(color), alpha(alpha), wrap(wrap), font(font), interline(interline), center(center) {}

void Text::layout() {
    textSize=int2(0,size);
    int wrap = this->wrap;
    if(center) {
        TextLayout layout(text, size, wrap, font, interline, false); // Layouts without justification
        wrap = layout.maxLength;
    }
    TextLayout layout(text, size, wrap, font, interline, true);
    textSize = int2(layout.maxLength, layout.penY);

    textLines.clear(); textLines.reserve(layout.text.size);
    cursor=Cursor(0,0); uint currentIndex=0;
    for(const TextLayout::TextLine& line: layout.text) {
        TextLine textLine;
        for(const TextLayout::Character& o: line) {
            currentIndex = o.editIndex;
            if(currentIndex<=editIndex) { // Restores cursor after relayout
                cursor = Cursor(textLines.size, textLine.size);
            }
            if(o.font) {
                const Glyph& glyph=o.font->glyph(o.index,o.pos.x);
                Character c{int2(o.pos)+glyph.offset, share(glyph.image), o.editIndex, int(o.pos.x+o.advance/2), (int)glyph.image.height, int(o.advance)};
                textLine << move(c);
            } else { // Format character
                textLine << Character{int2(o.pos),Image(),o.editIndex,int(o.pos.x+o.advance/2), this->size, int(o.advance)};
            }
        }
        currentIndex++;
        if(currentIndex<=editIndex) cursor = Cursor(textLines.size, textLine.size); // End of line
        textLines << move(textLine);
    }
    if(!text.size) { assert(editIndex==0); cursor = Cursor(0,0); }
    else if(currentIndex<=editIndex) { assert(textLines); cursor = Cursor(textLines.size-1, textLines.last().size); } // End of text
    links = move(layout.links);
    for(TextLayout::Line layoutLine: layout.lines) {
        for(uint line: range(layoutLine.begin.line, layoutLine.end.line+1)) {
            const TextLayout::TextLine& textLine = layout.text[line];
            if(layoutLine.begin.column<textLine.size) {
                TextLayout::Character first = (line==layoutLine.begin.line) ? textLine[layoutLine.begin.column] : textLine.first();
                TextLayout::Character last = (line==layoutLine.end.line && layoutLine.end.column<textLine.size) ? textLine[layoutLine.end.column] : textLine.last();
                assert(first.pos.y == last.pos.y);
                lines << Line{ int2(first.pos+vec2(0,1)), int2(last.pos+vec2(last.font?last.font->advance(last.index):0,2))};
            }
        }
    }
}
int2 Text::sizeHint() {
    if(!textSize) layout();
    return max(minSize,textSize);
}
void Text::render() { render(target, max(int2(0), int2(center ? (target.size().x-textSize.x)/2 : 0, (target.size().y-textSize.y)/2))); }
void Text::render(const Image& target, int2 offset) {
    if(!textSize) layout();
    for(const TextLine& line: textLines) for(const Character& b: line) if(b.image) blit(target, offset+b.pos, b.image, color, alpha);
    for(const Line& l: lines) fill(target, offset+Rect(l.min,l.max), black);
}

bool Text::mouseEvent(int2 position, int2 size, Event event, Button button) {
    if(event==Release || (event==Motion && !button)) return false;
    position -= max(int2(0),(size-textSize)/2);
    if(!Rect(textSize).contains(position)) return false;
    for(uint line: range(textLines.size)) {
        if(position.y < (int)(line*this->size) || position.y > (int)(line+1)*this->size) continue;
        const TextLine& textLine = textLines[line];
        if(!textLine) goto break_;
        // Before first character
        const Character& first = textLine.first();
        if(position.x <= first.center) { cursor = Cursor(line,0); goto break_; }
        // Between characters
        for(uint column: range(0,textLine.size-1)) {
            const Character& prev = textLine[column];
            const Character& next = textLine[column+1];
            if(position.x >= prev.center && position.x <= next.center) { cursor = Cursor(line,column+1); goto break_; }
        }
        // After last character
        const Character& last = textLine.last();
        if(position.x >= last.center) { cursor = Cursor(line,textLine.size); goto break_; }
    }
    if(event == Press && textClicked) { textClicked(); return true; }
    break_:;
    if(event == Press) for(const Link& link: links) if(link.begin<cursor && cursor<link.end) { linkActivated(link.identifier); return true; }
    if(event == Press && textClicked) { textClicked(); return true; }
    return false;
}

uint Text::index() {
    if(!textLines) return 0;
    if(cursor.line==textLines.size) return textLines.last().last().editIndex;
    assert(cursor.line<textLines.size,cursor.line,textLines.size);
    assert(cursor.column<=textLines[cursor.line].size, cursor.column, textLines[cursor.line].size);
    if(cursor.column<textLines[cursor.line].size) {
        uint index = textLines[cursor.line][cursor.column].editIndex;
        assert(index<text.size);
        return index;
    }
    uint index = 1; // ' ', '\t' or '\n' immediately after last character
    uint line=cursor.line;
    while(line>0 && !textLines[line]) line--, index++; //count \n (not included as characters)
    if(textLines[line]) index += textLines[line].last().editIndex;
    return index;
}

/// TextInput

bool TextInput::mouseEvent(int2 position, int2 size, Event event, Button button) {
    setCursor(position+Rect(size),::Cursor::Text);
    if(event==Press) setFocus(this);
    if(event==Press && button==MiddleButton) {
        Text::mouseEvent(position,size,event,button);
        array<uint> selection = toUTF32(getSelection());
        if(!text) { editIndex=selection.size; text=move(selection); }
        else { editIndex=index()+selection.size; array<uint> cat; cat<<text.slice(0,index())<<selection<<text.slice(index()); text = move(cat); }
        layout();
        if(textChanged) textChanged(toUTF8(text));
        return true;
    }
    Cursor cursor;
    bool contentChanged = Text::mouseEvent(position,size,event,button) || this->cursor!=cursor;
    if(event==Press && button==LeftButton) { selectionStart = cursor; return true; }
    return contentChanged;
}

bool TextInput::keyPress(Key key, Modifiers modifiers) {
    cursor.line=min<uint>(cursor.line,textLines.size-1);
    const TextLine& textLine = textLines[cursor.line];

    if(modifiers&Control && key=='v') {
        array<uint> selection = toUTF32(getSelection(true));
        if(!text) { text=move(selection); editIndex=selection.size; }
        else { editIndex=index()+selection.size; array<uint> cat; cat<<text.slice(0,index())<<selection<<text.slice(index()); text = move(cat); }
        layout();
        if(textChanged) textChanged(toUTF8(text));
        return true;
    }

    if(key==UpArrow) {
        if(cursor.line>0) cursor.line--;
    } else if(key==DownArrow) {
         if(cursor.line<textLines.size-1) cursor.line++;
    } else {
        cursor.column=min<uint>(cursor.column,textLine.size);

        /**/  if(key==LeftArrow) {
            if(cursor.column>0) cursor.column--;
            else if(cursor.line>0) cursor.line--, cursor.column=textLines[cursor.line].size;
        }
        else if(key==RightArrow) {
            if(cursor.column<textLine.size) cursor.column++;
            else if(cursor.line<textLines.size-1) cursor.line++, cursor.column=0;
        }
        else if(key==Home) cursor.column=0;
        else if(key==End) cursor.column=textLine.size;
        else if(key==Delete) {
            if(cursor.column<textLine.size || cursor.line<textLines.size-1) {
                text.removeAt(editIndex=index()); layout(); if(textChanged) textChanged(toUTF8(text));
            }
        }
        else if(key==Backspace) { //LeftArrow+Delete
            if(cursor.column>0) cursor.column--;
            else if(cursor.line>0) cursor.line--, cursor.column=textLines[cursor.line].size;
            else return false;
            if(index()<text.size) {
                text.removeAt(editIndex=index()); layout(); if(textChanged) textChanged(toUTF8(text));
            }
        }
        else if(key==Return) {
            if(textEntered) textEntered(toUTF8(text));
            else {
                editIndex=index()+1; text.insertAt(index(),'\n'); layout(); if(textChanged) textChanged(toUTF8(text));
            }
        }
        else {
            ref<uint> keypadNumbers = {KP_0, KP_1, KP_2, KP_3, KP_4, KP_5, KP_6, KP_7, KP_8, KP_9};
            char c=0;
            if(key>=' ' && key<=0xFF) c=key; //TODO: UTF8 Compose
            else if(keypadNumbers.contains(key)) c='0'+keypadNumbers.indexOf(key);
            else if(key==KP_Asterisk) c='*'; else if(key==KP_Plus) c='+'; else if(key==KP_Minus) c='-'; else if(key==KP_Slash) c='/';
            else return false;
            editIndex=index()+1; if(text) text.insertAt(index(), c); else text<<c, editIndex=1; layout(); if(textChanged) textChanged(toUTF8(text));
        }
    }
    if(!(modifiers&Shift)) selectionStart=cursor;
    return true;
}

void TextInput::render() {
    Text::render();
    if(hasFocus(this)) {
        assert(cursor.line < textLines.size, cursor.line, textLines.size);
        const TextLine& textLine = textLines[cursor.line];
        int x = 0;
        if(cursor.column<textLine.size) x= textLine[cursor.column].pos.x;
        else if(textLine) x=textLine.last().pos.x+textLine.last().advance;
        int2 offset = max(int2(0),(target.size()-textSize)/2);
        fill(target, offset+int2(x,cursor.line*size)+Rect(2,size), black);
    }
}
