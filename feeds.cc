#include "feeds.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "html.h"
#include "interface.h"
#include "window.h"

ICON(network);

Entry::Entry(Entry&& o) : Item(move(o)) { link=move(o.link); content=o.content; o.content=0; isHeader=o.isHeader; }
Entry::Entry(string&& name, string&& link, Image&& icon):Item(move(icon),move(name)),link(move(link)){}
Entry::~Entry() { if(content) delete content; }
#include "array.cc"
template struct array<Entry>;
template struct Array<Entry>;
template struct ListSelection<Entry>;
template struct List<Entry>;

template struct Scroll<HTML>;

Feeds::Feeds() {
    window.localShortcut("Escape"_).connect(&window, &Window::hide);
    window.localShortcut("Right"_).connect(this, &Feeds::readNext);
#if __arm__
    buttons.keyPress[KEY_POWER].connect(&window, &Window::hide);
    buttons.keyPress[BTN_EXTRA].connect(this, &Feeds::readNext);
#endif
    readConfig = appendFile(".config/read"_,home());
    read = split(readFile(".config/read"_,home()),'\n');
    reserve(256); //realloc would invalidate delegates
    List<Entry>::activeChanged.connect(this,&Feeds::activeChanged);
    List<Entry>::itemPressed.connect(this,&Feeds::itemPressed);
    array<string> feeds = split(readFile(".config/feeds"_,home()),'\n');
    for(const string& url: feeds) getURL(url, Handler(this, &Feeds::loadFeed), 20*60);
}
Feeds::~Feeds() { close(readConfig); }

bool Feeds::isRead(const Entry& entry) {
    const Text& text = entry.get<Text>();
    string id = text.text+entry.link;
    return contains(read,id);
}
void Feeds::setRead(const Entry& entry) {
    const Text& text = entry.get<Text>();
    string id = text.text+entry.link;
    id=replace(id,"\n"_,""_);
    if(contains(read,id)) return;
    ::write(readConfig,string(id+"\n"_));
    read << move(id);
}
void Feeds::setAllRead() {
    for(Entry& entry: *this) {
        setRead(entry);
    }
}

void Feeds::loadFeed(const URL& url, array<byte>&& document) {
    Element feed = parseXML(move(document));

    //Header
    string title = feed.text("rss/channel/title"_); //RSS
    if(!title) title = feed("feed"_)("title"_).text(); //Atom
    if(!title) { warn("Invalid feed"_,url); return; }
    array<string> words = split(title,' ');
    if(words.size()>4) title=join(slice(words,0,4)," "_);

    string link = feed.text("rss/channel/link"_); //RSS
    if(!link) link = feed("feed"_)("link"_)["href"_]; //Atom
    if(!link) { warn("Invalid feed"_,url); return; }

    Entry header(move(title),copy(link));
    header.isHeader=true;
    append( move(header) );

    string favicon = cacheFile(URL(link).relative("/favicon.ico"_));
    if(exists(favicon,cache)) {
        last().get<Icon>().image = resize(decodeImage(readFile(favicon,cache)),16,16);
    } else {
        last().get<Icon>().image = resize(networkIcon,16,16);
        getURL(link, Handler(this, &Feeds::getFavicon), 7*24*60*60);
    }

    array<Entry> items; int history=0;
    auto addItem = [this,&history,&items](const Element& e)->void{
        if(history++>=64) return; //avoid getting old unreads on feeds with big history
        if(items.size()>=32) return;
        string text=e("title"_).text();
        text=trim(unescape(text));

        string url=e("link"_).text(); //RSS
        if(!url) url=e("link"_)["href"_]; //Atom

        Entry entry(move(text),move(url));
        if(!isRead(entry)) items.append(move(entry));
    };
    feed.xpath("feed/entry"_,addItem); //Atom
    feed.xpath("rss/channel/item"_,addItem); //RSS
    int preload=0;
    for(int i=items.size()-1;i>=0;i--) { //oldest first
        append(move(items[i]));
        Entry& item = last(); //reference shouldn't move while loading
        if(preload++<=16) { //preload first items
            item.content = new Scroll<HTML>;
            item.content->go(item.link); //preload
        }
    }
    contentChanged.emit();
}

void Feeds::getFavicon(const URL& url, array<byte>&& document) {
    Element page = parseHTML(move(document));
    string icon;
    page.xpath("html/head/link"_,[&icon](const Element& e)->void{ if(e["rel"_]=="shortcut icon"_||(!icon && e["rel"_]=="icon"_)) icon=e["href"_]; } );
    if(!icon) icon="/favicon.ico"_;
    if(url.relative(icon).path!=url.relative("/favicon.ico"_).path) symlink("../"_+cacheFile(url.relative(icon)),cacheFile(url.relative("/favicon.ico"_)),cache);
    for(Entry& entry: *this) {
        if(contains(entry.link,url.host)) {
            new ImageLoader(url.relative(icon), &entry.get<Icon>().image, contentChanged, int2(16,16), 7*24*60*60);
            break; //only header
        }
    }
}

void Feeds::activeChanged(int index) {
    Entry& entry = array::at(index);
    if(entry.isHeader) return;
    if(!isRead(entry)) {
        Text& text = entry.get<Text>();
        setRead(entry);
        text.setSize(12);
    }
    contentChanged.emit();
}

void Feeds::itemPressed(int index) {
    Entry& entry = array::at(index);
    if(content) delete content; //release read items
    if(!entry.content) entry.content = new Scroll<HTML>;
    content = entry.content; entry.content=0; //move preloaded content to window
    content->contentChanged.disconnect(&window);
    window.widget = &content->parent();
    window.setTitle(entry.get<Text>().text);
    window.setIcon(entry.get<Icon>().image);
    if(!content->url) content->go(entry.link);
    if(window.visible) {
        window.widget->size = window.size;
        window.widget->update();
        window.render();
    } else {
        window.setSize(int2(0,0));
        window.show();
    }
    content->contentChanged.connect(&window, &Window::update);
}

void Feeds::readNext() {
    uint i=index;
    for(;;) {
        i++;
        if(i>=count()) {
            execute("/bin/sh"_,{"-c"_,"killall desktop; desktop&"_}); //FIXME: release leaked memory
            return;
        }
        if(!array::at(i).isHeader && !isRead(array::at(i))) break;
    }
    setActive(i);
    itemPressed(i);
}
