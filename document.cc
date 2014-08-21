#include "data.h"
#include "text.h"
#include "layout.h"
#include "window.h"
#include "interface.h"
//#include "png.h"
//#include "jpeg.h"
#include "pdf.h"

static constexpr int oversample = 1;

struct Placeholder : Widget {
    void render() {}
};

/// Header of a section from a \a Document
struct Header {
    array<uint> indices; // Header index for each level
    String name; // Header name
    uint page; // Header page index
};

/// \a Page from a \a Document
struct Page : VBox {
    uint index;
    float marginPx;
    array<Header> headers;
    array<unique<Widget>> elements;
    array<unique<Image>> images;
    Widget* footer = 0;

    Page(Linear::Extra main, uint index, float marginPx)
        : Linear(main, Linear::Expand, true), index(index), marginPx(marginPx) {}

    void render() override {
        Image page = share(this->target);
        Image inner = clip(page, Rect(int2(marginPx,marginPx), page.size() - int2(marginPx,marginPx)));
        fill(target, Rect(target.size()), !(sizeHint().y <= inner.size().y) ? vec3(3./4,3./4,1) : white);
       {this->target = share(inner);
            VBox::render();
        this->target = share(page);}
        Image footer = clip(page, Rect(int2(0, page.size().y - marginPx), page.size()));
        if(this->footer) this->footer->render(footer);
    }
};

struct Format {
    const int2 pageSize;
    float marginPx;
    const string font;
    const float footerSize, textSize, headerSize, titleSize;
    Linear::Extra titlePageLayout, pageLayout;
};
struct A4 : Format {
#if 0
    static constexpr int pageWidth = 1050, pageHeight = 1485;
    static constexpr float inchPx = pageHeight / (297/*mm*/ / 25.4/*inch/mm*/);
#else
    static constexpr float inchMM = 25.4, inchPx = 90;
    static constexpr int pageWidth = 210/*mm*/ * (inchPx/inchMM), pageHeight = 297/*mm*/ * (inchPx/inchMM);
#endif
    static constexpr float pointPx = inchPx / 72;
    A4() : Format{int2(pageWidth, pageHeight), 1.5 * inchPx, "FreeSerif"_, 12 * pointPx, 12 * pointPx, 14 * pointPx, 16 * pointPx,
                  Linear::Center, Linear::Share} {}
};

/// Layouts a document
struct Document {
    const String source;
    string formatString;
    Format format = formatString == "A4"_ ? A4() : Format{int2(1050,768), 64, "DejaVuSans"_, 0, 24, 24, 32, Linear::Spread, Linear::Spread};
    const int2 pageSize= oversample*format.pageSize;
    const float marginPx = oversample*format.marginPx;
    const int2 contentSize = pageSize - int2(2*marginPx);
    const float textSize = oversample*format.textSize;
    const float headerSize = oversample*format.headerSize;
    const float titleSize = oversample*format.titleSize;
    const float interlineStretch = 3./2;

    // Document properties
    array<Header> headers;
    array<string> pages; // Source slices for each page
    array<array<uint>> indices; // Level counters state at start of each page for correct single page rendering

    /// Generates page starts table, table of contents
    Document(String&& source_) : source(move(source_)), formatString(startsWith(source,"%"_)?section(source.slice(1),'\n'):"A4"_) {
        assert_(contentSize > int2(0), pageSize, marginPx);
        assert_(!source.contains('\r'));
        array<uint> indices;
        for(TextData s (source); s;) {
            uint start = s.index;
            this->indices << copy(indices);
            Page page = parsePage(s, indices, pages.size, true);
            pages << source(start, s.index);
            headers << move(page.headers);
        }
    }

    /// Registers a new element and returns it
    template<Type T, Type... Args> T& element(Page& page, Args&&... args) const {
        unique<T> t(forward<Args>(args)...);
        T* pointer = t.pointer;
        page.elements << unique<Widget>(move(t));
        return *pointer;
    }
    /// Registers a new text element and returns it
    Text& newText(Page& page, string text, int size, bool center=true) const {
        return element<Text>(page, text, size, 0, 1, contentSize.x, format.font, false, interlineStretch, center);
    }

    // Parser

    template<Type... Args> String warn(TextData& s, const Args&... args) const {
        String text = str(s.lineIndex)+": "_+str(args...); log(text); return text;
    }
    template<Type... Args> Text& warnText(TextData& s, Page& page, const Args&... args) const {
        return newText(page, warn(s, args...), textSize);
    }

    /// Skips whitespaces and comments
    void skip(TextData& s) const {
        for(;;) {
            s.whileAny(" \n"_);
            if(s.match('%')) s.line(); // Comment
            else break;
        }
    }

    /// Parses a subscript expression
    String parseSubscript(TextData& s, const ref<string>& delimiters) const {
        ref<string> lefts {"["_,"{"_,"⌊"_};
        ref<string> rights{"]"_,"}"_,"⌋"_};

        String subscript;
        if(!s.wouldMatchAny(lefts)) subscript << s.next();
        for(;;) {
            assert_(s, subscript);
            if(s.wouldMatchAny(delimiters)) break;
            else if(s.match('_')) subscript << parseSubscript(s, delimiters);
            else {
                for(int index: range(lefts.size)) {
                    if(s.match(lefts[index])) {
                        if(index>=2) subscript << lefts[index];
                        String content = parseLine(s, {rights[index]}, true);
                        assert_(content);
                        subscript << content;
                        if(index>=2) subscript << rights[index];
                        goto break_;
                    }
                } /*else*/
                if(s.wouldMatchAny(" \t\n,;()^/+-|"_) || s.wouldMatchAny({"·"_,"⌋"_,"²"_})) break;
                else subscript << s.next();
                break_:;
            }
        }
        assert_(s && subscript.size && subscript.size<=15, "Expected subscript end delimiter  _]), got end of document",
                "'"_+subscript.slice(0, min(subscript.size, 16ul))+"'"_, subscript.size, delimiters, s.buffer);
        String text;
        text << (char)(TextFormat::SubscriptStart);
        text << subscript;
        text << (char)(TextFormat::SubscriptEnd);
        return text;
    }

    /// Parses a line expression
    String parseLine(TextData& s, const ref<string>& delimiters={"\n"_}, bool match=true) const {
        String text; bool bold=false,italic=false;
        while(s) { // Line
            if(match ? s.matchAny(delimiters) : s.wouldMatchAny(delimiters)) break;

            /**/ if(s.match('*')) { text << (char)(TextFormat::Bold); bold=!bold; }
            else if(s.match("//"_)) text << "/"_;
            else if(s.match('/')) { text << (char)(TextFormat::Italic); italic=!italic; }
            else if(s.match('\\')) text << s.next();
            else if(s.match('_')) text << parseSubscript(s, delimiters);
            else if(s.match('^')) {
                text << (char)(TextFormat::Superscript);
                String superscript = parseLine(s,{" "_,"."_,":"_,"\n"_,"("_,")"_,"^"_,"/"_,"|"_,"·"_,"⌋"_}, false);
                if(superscript.size<1 || superscript.size>14) {
                    log("Expected superscript end delimiter  ^]), got end of document", superscript.size, "'"_+superscript+"'"_);
                    log("'"_+(string)text.slice(0, min(text.size,14ul))+"'"_);
                    log(s.buffer);
                    error(""_);
                }
                text << superscript;
                text << (char)(TextFormat::Superscript);
            }
            else text << s.next();
        }
        if(bold) warn(s, "Expected bold end delimiter *, got end of line");
        if(italic) warn(s, "Expected italic end delimiter /, got end of line");
        return text;
    }

    /// Parses a layout expression
    Widget* parseLayout(TextData& s, Page& page, bool quick) const {
        array<Widget*> children;
        char type = 0;
        int start = s.index;
        while(!s.match(')')) {
            // Element
            skip(s);
            if(s.match('(')) children << parseLayout(s, page, quick);
            else if(s.wouldMatchAny("&_$^@"_)) {
                string prefix = s.whileAny("$&_^@"_);
                string path = s.whileNo(" \t\n)-|+"_);
                assert_(s);
                if(quick) {
                    //TODO: Layout image size for page numbers in table of contents with auto page break
                    children << &element<Placeholder>(page);
                } else {
                    unique<Image> image;
                    /**/ if(existsFile(path)) image = unique<Image>(decodeImage(readFile(path)));
                    else if(existsFile(path+".png"_)) image = unique<Image>(decodeImage(readFile(path+".png"_)));
                    else if(existsFile(path+".jpg"_)) image = unique<Image>(decodeImage(readFile(path+".jpg"_)));
                    if(image) {
                        for(char operation: prefix.reverse()) {
                            if(operation=='@') image = unique<Image>(rotate(image));
                            else if(operation=='^') image = unique<Image>(upsample(image));
                            else if(operation=='_') image = unique<Image>(downsample(image));
                            else if(operation=='$') { for(byte4& v: image->buffer) if(!v.r && !v.g && !v.b) v=0xFF; } // Sets 0 to FF
                            else if(operation=='&') {}
                            else error("Unknown image operation", operation);
                        }
                        while(!(image->size() <= contentSize)) image = unique<Image>(downsample(image));
                        children << &element<ImageWidget>(page, image);
                        page.images << move(image);
                    }
                    else warn(s, "Missing image", path);
                }
            } else {
                String text;
                if(s.match('"')) text = parseLine(s, {"\""_}, true);
                else text = parseLine(s, {"|"_,"-"_,"+"_,")"_}, false);
                assert_(trim(text)==simplify(copy(text)), trim(text),"\n"_,simplify(copy(text)));
                children << &newText(page, trim(text), textSize);
                if(s.match('\n')) continue;
                assert_(s);
            }
            skip(s);
            // Separator
            /**/ if(s.match(')')) break;
            else if(s.wouldMatchAny("-|+"_)) type = s.next();
            else if(!s.match(type)) {
                children << &warnText(s, page, "Expected"_+(type?str(type):"-, |, +"_," or )"_));
                break;
            }
        }
        if(type=='-') return &element<VBox>(page, move(children), VBox::Spread, VBox::AlignCenter, false/*true*/);
        else if(type=='|') return &element<HBox>(page, move(children), HBox::Share, HBox::AlignCenter, true);
        else if(type=='+') return &element<WidgetGrid>(page, move(children), false/*true*/);
        //else if(type=='*') return &element<WidgetGrid>(page, move(children), false);
        else if(!type) {
            if(!children) return &warnText(s, page, "Empty layout", s.buffer(start-1,s.index));
            assert_(children.size==1);
            return children.first();
        }
        else error("Unknown layout type", type);
    }

    /// Parses a page statement
    /// \arg quick Quick layout for table of contents (skips images)
    Page parsePage(TextData& s, array<uint>& indices, uint pageIndex, bool quick=false) const {
        Page page (pageIndex ? format.pageLayout : format.titlePageLayout, pageIndex, marginPx);
        while(s) {
            while(s.match('%')) s.line(); // Comment

            if(s.match('(')) { // Float
                page << parseLayout(s, page, quick);
                s.match("\n"_);
                continue;
            }

            if(s.match('-')) { // List
                VBox& list = element<VBox>(page, VBox::Even);
                do {
                    list << &newText(page, "- "_+parseLine(s), textSize, false);
                } while(s.match('-'));
                page << &list;
                continue;
            }

            bool center=false;
            if(s.match(' ')) {
                assert_(!center);
                center = true;
            }

            int size = textSize;
            bool bold = false;
            if(s.match('!')) {
                assert_(!bold);
                bold = true;
                size = titleSize;
                center = true;
            }

            uint level = 0;
            while(s.match('#')) {
                level++;
                //center=true;
                size = headerSize;
            }

            if(s.match('\\')) {
                string command = s.whileNot('\n');
                if(command == "tableOfContents"_) {
                    auto& vbox = element<VBox>(page, Linear::Top, Linear::Expand);
                    for(const Header& header: headers) {
                        String text;
                        text << repeat(" "_, header.indices.size);
                        if(header.indices.size<=1) text << (char)(TextFormat::Bold);
                        for(int level: header.indices) text << dec(level) << '.';
                        text << ' ' << header.name;
                        auto& hbox = element<HBox>(page, Linear::Spread);
                        hbox << &newText(page, text, textSize);
                        hbox << &newText(page, " "_+dec(header.page), textSize);
                        vbox << &hbox;
                    }
                    page << &vbox;
                } else error(command);
                continue;
            }

            String text = center ? String(""_) : repeat(" "_, 4);

            if(level) {
                if(level > indices.size) indices.grow(level);
                if(level < indices.size) indices.shrink(level);
                indices[level-1]++;
                /*if(level==1)*/ bold = true;
            }

            if(bold) text << (char)(TextFormat::Bold);
            if(level) {
                for(int level: indices) text << dec(level) << '.';
                text << ' ';
            }

            String userText;
            while(s && !s.match('\n')) userText << parseLine(s) << ' ';
            if(userText) {
                text << trim(userText);
                if(level && pageIndex==pages.size) page.headers << Header{copy(indices), move(userText), pageIndex};
                page << &newText(page, text, size, center);
            }

            if(s.match('\n')) break; // Page break
        }
        if(format.footerSize && page.index) page.footer = &newText(page, dec(page.index), textSize);
        return move(page);
    }
    Page parsePage(TextData&& s, array<uint>&& indices, uint pageIndex) const { return parsePage(s, indices, pageIndex); }
    Page parsePage(uint pageIndex) const {
        assert_(pageIndex<pages.size, pageIndex, pages.size);
        return parsePage(TextData(pages[pageIndex]), copy(indices[pageIndex]), pageIndex);
    }

#if RASTERIZED_PDF
    array<Image> images() {
        return apply(pages.size, [this](int index){ Image target(pageSize); parsePage(index).Widget::render(target); return move(target); });
    }
    buffer<byte> toPDF() { return ::toPDF(images()); }
#else
    buffer<byte> toPDF() {
        uint pageIndex = 0;
        for(TextData s (source); s;) {
            parsePage(pageIndex);
            pageIndex++;
        }
        pageCount = pageIndex;
    }
#endif
};

struct PageView : Widget {
    int pageCount;
    function<Page(int)> getPage;
    int pageIndex;
    Page page = getPage(pageIndex);

    PageView(int pageCount, function<Page(int)> getPage, int pageIndex=0)
        : pageCount(pageCount), getPage(getPage), pageIndex(pageIndex) {}

    bool keyPress(Key key, Modifiers) override {
        /**/ if(key == LeftArrow) pageIndex = max(0, pageIndex-1);
        else if(key == RightArrow) pageIndex = min(pageCount-1, pageIndex+1);
        else return false;
        page = getPage(pageIndex);
        return true;
    }

    bool mouseEvent(int2, int2, Event, Button button) override {
        setFocus(this);
        /**/ if(button==WheelUp) pageIndex = max(0, pageIndex-1);
        else if(button==WheelDown) pageIndex = min(pageCount-1, pageIndex+1);
        else return false;
        page = getPage(pageIndex);
        return true;
    }

    void render() override {
        /***/ if(oversample==1) page.Widget::render(target);
        else if(oversample==2) {
            Image large(2*target.size());
            page.Widget::render(large);
            downsample(target, large);
        } else error(oversample);
    }
};

struct DocumentViewer {
    const string path;
    const String lastPageIndexPath = "."_+path+".last-page-index"_;
    const int lastPageIndex = existsFile(lastPageIndexPath) ? fromInteger(readFile(lastPageIndexPath)) : 0;
    Document document { readFile(path) };
    PageView view {(int)document.pages.size, {this, &DocumentViewer::getPage}, lastPageIndex};
    Window window {&view, document.format.pageSize, dec(view.pageIndex)};

    Page getPage(int pageIndex) {
        if(window) {
            window.render();
            window.setTitle(dec(pageIndex));
            writeFile(lastPageIndexPath, dec(pageIndex));
        }
        return document.parsePage(pageIndex);
    }

    FileWatcher watcher{path, [this](string){ //TODO: watch images
            document.~Document(); new (&document) Document(readFile(path));
            view.pageCount = document.pages.size;
            view.page = document.parsePage(view.pageIndex);
            window.render();
            window.show();
        } };

    DocumentViewer(const string path) : path(path) { window.background=NoBackground; }
};

struct DocumentApp {
    unique<DocumentViewer> viewer = nullptr;
    DocumentApp() {
        assert_(arguments().size==2 && ref<string>({"preview"_,"export"_}).contains(arguments()[1]), "Usage: <path> preview|export");
        string path = arguments()[0], command = arguments()[1];
        /***/ if(command=="preview"_) viewer = unique<DocumentViewer>(path);
        else if(command=="export"_) writeFile(section(path,'.',0,-2)+".pdf"_, Document(readFile(path)).toPDF());
        else error("Unknown command"_, arguments());
    }
} app;
