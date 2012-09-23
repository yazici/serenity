#pragma once
#include "interface.h"
#include "html.h"
#include "file.h"
#include "ico.h"

/// Entry is an \a Item with a \a link to an article
struct Entry : Item {
    string guid, link;
    Entry(string&& guid, string&& link, Image&& icon, string&& text, int size=16):Linear(Left),Item(move(icon),move(text),size),guid(move(guid)),link(move(link)){}
};

/// FavIcon is an \a Image shared between several \a Entries
struct FavIcon {
    const string host;
    Image image;
    array<Image*> users;
    signal<> imageChanged;
    /// Loads favicon for \a host
    FavIcon(string&& host);
    /// Parses HTML link elements to find out favicon location
    void get(const URL& url, Map&& document);
    /// Updates shared users when receiving the real favicon
    void update();
};

/// Feeds is a list of entries fetched from RSS/Atom feeds
/// \note .config/feeds contains the list of feeds to fetch, .config/read contains the list of read articles
struct Feeds : List<Entry> {
    File readConfig;
    Map readMap;
    signal<> listChanged;
    signal< const ref<byte>& /*link*/, const ref<byte>& /*title*/, const Image& /*favicon*/ > pageChanged;
    array<FavIcon*> favicons; //store strong references to favicons weakly referenced by entries (on heap because of ImageLoader)

    Feeds();
    /// Polls all feeds
    void load();
    /// Returns whether the entry with \a title and \a link is read (according to config/read)
    bool isRead(const ref<byte>& title, const ref<byte>& link);
    /// Returns whether \a entry is read (according to config/read)
    bool isRead(const Entry& entry);
    /// Loads an RSS/Atom feed
    void loadFeed(const URL&, Map&& document);
    /// If unread, appends entry at \a index to config\read
    void setRead(uint index);
    /// Sends pageChanged signal and preload next page
    void readEntry(uint index);
    /// Sets active entry as read, activate next unread entry
    void readNext();
};
