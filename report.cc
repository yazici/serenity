#include "data.h"
#include "text.h"
#include "layout.h"
#include "window.h"
#include "interface.h"
#include "png.h"
//#include "jpeg.h"

struct Document : Widget {
    static constexpr int2 windowSize = int2(1080, 1527);
    //const int2 windowSize = int2(532, 752);
    static constexpr int oversample = 1;
    const int2 previewSize = oversample * windowSize;

    static constexpr bool showMargins = true;
    static constexpr int2 pageSize = int2(oversample * windowSize.x, oversample * windowSize.y);
    static constexpr float pageHeightMM = 297;
    static constexpr float inchMM = 25.4;
    static constexpr float pxMM = pageSize.y / pageHeightMM;
    static constexpr float marginPx = 1.5 * inchMM * pxMM;
    const int2 contentSize = pageSize - int2(2*marginPx); //oversample * int2(round(vec2(windowSize) * (1-(showMargins?2*margin:0))));

    static constexpr float pointMM = 0.3527;
    static constexpr float pointPx = pointMM * pxMM;
    static constexpr float textSize = 12 * pointPx;
    static constexpr float headerSize = 14 * pointPx;
    static constexpr float titleSize = 16 * pointPx;
    static_assert(pageSize.y / (pageHeightMM / inchMM) > 130, ""); // 261.18

    const string font = "LiberationSerif"_; //"FreeSerif"_;
    const float interlineStretch = 3./2;

    TextData s;

    int pageIndex=0, pageCount;

    array<unique<Widget>> elements;
    array<unique<Image>> images;

    array<uint> levels;
    struct Entry { array<uint> levels; String name; uint page; };
    array<Entry> tableOfContents;

    int viewPageIndex = 2; //FIXME: persistent
    signal<int> pageChanged;

    Document(string source) : s(filter(source, [](char c) { return c=='\r'; })) {
        while(s) { layoutPage(Image()); pageIndex++; } // Generates table of contents
        pageCount=pageIndex;
    }

    template<Type T, Type... Args> T& element(Args&&... args) {
        unique<T> t(forward<Args>(args)...);
        T* pointer = t.pointer;
        elements << unique<Widget>(move(t));
        return *pointer;
    }
    Text& newText(string text, int size, bool center=true) { return element<Text>(text, size, 0, 1, contentSize.x, font, interlineStretch, center); }

    ref<string> lefts {"["_,"{"_,"⌊"_};
    ref<string> rights{"]"_,"}"_,"⌋"_};

    String parseSubscript(TextData& s, const ref<string>& delimiters) {
        String subscript;
        if(!s.wouldMatchAny(lefts)) subscript << s.next();
        for(;;) {
            assert_(s, subscript);
            if(s.wouldMatchAny(delimiters)) break;
            else if(s.match('_')) subscript << parseSubscript(s, delimiters);
            else {
                for(int index: range(lefts.size)) {
                    if(s.match(lefts[index])) {
                        if(lefts[index] != "["_) subscript << lefts[index];
                        String content = parseLine(s, {rights[index]}, true);
                        assert_(content);
                        subscript << content;
                        if(rights[index] != "]"_) subscript << rights[index];
                        goto break_;
                    }
                } /*else*/
                if(s.wouldMatchAny(" \t\n,()^/+|"_) || s.wouldMatchAny({"·"_,"⌋"_})) break; //if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||"+"_.contains(c))) break;
                else subscript << s.next(); //= s.identifier("+"); //s.whileNo(" ])/\n"_);
break_:;
            }
        }
        assert_(s && subscript.size && subscript.size<=15, "Expected subscript end delimiter  _]), got end of document", "'"_+subscript.slice(0, min(subscript.size, 16ul))+"'"_, subscript.size, delimiters);
        String text;
        text << (char)(TextFormat::SubscriptStart);
        text << subscript;
        text << (char)(TextFormat::SubscriptEnd);
        return text;
    }

    String parseLine(TextData& s, const ref<string>& delimiters={"\n"_}, bool match=true) {
        String text; bool bold=false,italic=false;
        for(;;) { // Line
            assert_(s);
            if(match ? s.matchAny(delimiters) : s.wouldMatchAny(delimiters)) break;

            /**/ if(s.match('*')) { text << (char)(TextFormat::Bold); bold=!bold; }
            else if(s.match("//"_)) text << "/"_;
            else if(s.match('/')) { text << (char)(TextFormat::Italic); italic=!italic; }
            else if(s.match('\\')) text << s.next();
            else if(s.match('_')) text << parseSubscript(s, delimiters);
            else if(s.match('^')) {
                text << (char)(TextFormat::Superscript);
                String superscript;
                for(;;) {
                    if(s.wouldMatchAny(" \t\n,()^/|"_) || s.wouldMatchAny({"·"_,"⌋"_})) break;
                    else superscript << s.next();
                }
                //= s.whileNo(" ^)/\n"_); //s.identifier("-+,[]"_); //s.whileNo(" ^])/\n"_);
                assert_(s && superscript.size && superscript.size<=14, "Expected superscript end delimiter  ^]), got end of document", superscript, superscript.size, text);
                text << superscript;
                text << (char)(TextFormat::Superscript);
            }
            else text << s.next();
        }
        assert_(!bold, "Expected bold end delimiter *, got end of line");
        assert_(!italic, "Expected italic end delimiter /, got end of line", text);
        return text;
    }

    String parseParagraph(TextData& s) {
        String text;
        while(s && !s.match('\n')) text << parseLine(s) << ' ';
        return text;
    }

    Widget* parseLayout(TextData& s) {
        array<Widget*> children;
        char type = 0;
        while(!s.match(')')) {
            // Element
            s.whileAny(" \t\n"_);
            if(s.match('(')) children << parseLayout(s);
            else if(s.wouldMatchAny("^&@"_)) {
                string type = s.whileAny("^&@"_);
                string path = s.untilAny(" \t\n"_);
                /**/ if(existsFile(path)) images << unique<Image>(decodeImage(readFile(path)));
                else if(existsFile(path+".png"_)) images << unique<Image>(decodeImage(readFile(path+".png"_)));
                else if(existsFile(path+".jpg"_)) images << unique<Image>(decodeImage(readFile(path+".jpg"_)));
                else error("Missing image", path);
                if(type=="@"_) images.last() = unique<Image>(rotate(images.last()));
                if(type=="^"_) images.last() = unique<Image>(upsample(images.last()));
                else if(!(images.last()->size() <= contentSize)) images.last() = unique<Image>(downsample(images.last()));
                children << &element<ImageWidget>(images.last());
            } else {
                String text = simplify(parseLine(s, {"|"_,"-"_,"+"_,")"_}, false));
                assert_(text, s.line());
                children << &newText(text, textSize);
            }
            // Separator
            s.whileAny(" \n"_);
            /**/ if(s.match(')')) break;
            else if(!type) type = s.next();
            else s.skip(string(&type,1));
        }
        if(type=='-') return &element<VBox>(move(children));
        else if(type=='|') return &element<HBox>(move(children));
        else if(type=='+') return &element<WidgetGrid>(move(children));
        else { assert_(children.size==1); return children.first(); }
    }

    void layoutPage(const Image& target) {
        elements.clear();
        images.clear();
        VBox page (pageIndex ? Linear::Share : Linear::Center, Linear::Expand);

        while(s) {
            if(s.match('%')) { // Comment
                s.line();
                continue;
            }

            if(s.match('(')) { // Float
                page << parseLayout(s);
                s.skip("\n"_);
                continue;
            }

            if(s.match('-')) { // List
#if 1
                VBox& list = element<VBox>(VBox::Even);
                do {
                    list << &newText("- "_+parseLine(s), textSize, false);
                } while(s.match('-'));
                page << &list;
#else
                String list;
                do {
                    list << "- "_+parseLine(s)+"\n"_;
                } while(s.match('-'));
                page << &newText(list, textSize, false);
#endif
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
                center=true;
            }

            if(s.match('\\')) {
                string command = s.whileNot('\n');
                if(command == "tableOfContents"_) {
                    auto& vbox = element<VBox>(Linear::Top, Linear::Expand);
                    for(const Entry& entry: tableOfContents) {
                        String header;
                        header << repeat(" "_, entry.levels.size);
                        if(entry.levels.size<=1) header << (char)(TextFormat::Bold);
                        for(int level: entry.levels) header << dec(level) << '.';
                        header << ' ' << entry.name;
                        auto& hbox = element<HBox>(Linear::Spread);
                        hbox << &newText(header, textSize);
                        hbox << &newText(" "_+dec(entry.page), textSize);
                        vbox << &hbox;
                    }
                    page << &vbox;
                } else error(command);
                continue;
            }

            String text = center ? String(""_) : repeat(" "_, 4);

            if(level) {
                if(level > levels.size) levels.grow(level);
                if(level < levels.size) levels.shrink(level);
                levels[level-1]++;
                /*if(level==1)*/ bold = true;
            }

            if(bold) text << (char)(TextFormat::Bold);
            if(level) {
                for(int level: levels) text << dec(level) << '.';
                text << ' ';
            }

            String userText = parseParagraph(s);
            if(userText) {
                text << trim(userText);
                if(level && !target) tableOfContents << Entry{copy(levels), move(userText), (uint)pageIndex};
                page << &newText(text, size, center);
            }

            if(s.match('\n')) break; // Page break
        }
        if(target) {
            if(showMargins) {
                //int2 margin = int2(round(this->margin * vec2(target.size())));
                Image inner = clip(target, Rect(int2(marginPx,marginPx), target.size() - int2(marginPx,marginPx)));
                fill(target, Rect(target.size()), !(page.sizeHint().y <= inner.size().y) ? vec3(3./4,3./4,1) : white);
                page.Widget::render(inner);
                Image footer = clip(target, Rect(int2(0, target.size().y - marginPx), target.size()));
                if(pageIndex) Text(dec(pageIndex), textSize, 0, 1, 0, font).Widget::render(footer);
            } else {
                fill(target, Rect(target.size()), !(page.sizeHint() <= target.size()) ? red : white);
                page.Widget::render(target);
            }
        }
    }

    bool keyPress(Key key, Modifiers) {
        /**/ if(key == LeftArrow) viewPageIndex = max(0, viewPageIndex-1);
        else if(key == RightArrow) viewPageIndex = min(pageCount-1, viewPageIndex+1);
        else return false;
        pageChanged(viewPageIndex);
        return true;
    }

    bool mouseEvent(int2, int2, Event, Button button) {
        setFocus(this);
        /**/ if(button==WheelUp) viewPageIndex = max(0, viewPageIndex-1);
        else if(button==WheelDown) viewPageIndex = min(pageCount-1, viewPageIndex+1);
        else return false;
        pageChanged(viewPageIndex);
        return true;
    }

    void clear() {
        s.index = 0;
        pageIndex=0;
        elements.clear();
        levels.clear();
    }
    void render() {
        clear();
        while(s && pageIndex < viewPageIndex) {
            // FIXME: parse levels
            // FIXME: auto page break with quick layout for previous pages
            s.until("\n\n\n"_);
            pageIndex++;
        }
        Image target (pageSize);
        layoutPage(target);
        Image image = clip(this->target, int2((pageIndex-viewPageIndex)*(target.size().x/oversample),0)+Rect(target.size()/oversample));
        if(oversample==1) copy(image, target);
        else if(oversample>=2) {
            for(int oversample=2; oversample<this->oversample; oversample*=2) target=downsample(target);
            downsample(image, target);
        }
        pageIndex++;
    }

    array<Image> renderPages() {
        clear();
        array<Image> pages;
        while(s) {
            Image target (pageSize);
            layoutPage(target);
            pages << move(target);
            pageIndex++;
        }
        return pages;
    }

    void writePages() {
        clear();
        array<String> args;
        Folder folder("Rapport.out"_, home(), true);
        while(s) {
            Image target (pageSize);
            layoutPage(target);
            log(pageIndex);
            writeFile(dec(pageIndex),encodePNG(target),folder);
            args << dec(pageIndex);
            pageIndex++;
        }
        args << String("rapport.pdf"_);
        log("PDF");
        execute(which("convert"_),toRefs(args),true,folder);
        log("Done");
    }
};

// -> file.cc
#include <sys/inotify.h>
/// Watches a folder for new files
struct FileWatcher : File, Poll {
    string path;
    /*const*/ uint watch;
    function<void(string)> fileModified;

    FileWatcher(string path, function<void(string)> fileModified)
        : File(inotify_init1(IN_CLOEXEC)), Poll(File::fd), path(path), watch(check(inotify_add_watch(File::fd, strz(path), IN_MODIFY))), fileModified(fileModified) {}
    void event() override {
        while(poll()) {
            ::buffer<byte> buffer = readUpTo(sizeof(struct inotify_event) + 256);
            inotify_event e = *(inotify_event*)buffer.data;
            fileModified(e.len ? string(e.name, e.len-1) : string());
            inotify_rm_watch(File::fd, watch);
            watch = check(inotify_add_watch(File::fd, strz(path), IN_MODIFY));
        }
    }
};
constexpr int2 Document::windowSize;
constexpr int2 Document::pageSize;

struct Report {
    string path = "rapport.txt"_;
    Document document {readFile(path)};
    Window window {&document, document.windowSize, "Report"_};
    FileWatcher watcher{path, [this](string){
            int index = document.viewPageIndex;
            document.~Document(); new (&document) Document(readFile(path)); // Reloads
            document.viewPageIndex = index;
            document.pageChanged.connect([&](int pageIndex){ window.setTitle(dec(pageIndex)); });
            window.render();
        } };
    Report() {
        assert_(arguments());
        /**/ if(arguments()[0]=="export"_) {
            document.writePages();
            exit(0);
            return;
        }
        else if(arguments()[0]=="preview"_) {
        }
        else error(arguments());
        window.actions[Escape]=[]{exit();}; window.background=White; window.focus = &document; window.show();
        document.pageChanged.connect([&](int pageIndex){ window.setTitle(dec(pageIndex)); });
    }

} app;
