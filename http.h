#pragma once
/// \file http.h DNS queries, asynchronous HTTP requests, persistent disk cache
#include "data.h"
#include "thread.h"
#include "function.h"
#include "file.h"
#include "image.h"

/// TCP network socket (POSIX)
struct TCPSocket : Socket {
    /// Connects to \a port on \a host
    TCPSocket(uint host, uint16 port);
};

/// SSL network socket (openssl)
struct SSLSocket : TCPSocket {
    SSLSocket(uint host, uint16 port, bool secure=false);
    ~SSLSocket();

    array<byte> readUpTo(int size);
    void write(const ref<byte>& buffer);

    handle<struct ssl_st*> ssl;
};

/// Implements Data::available using Stream::readUpTo
template<class T/*: Stream*/> struct DataStream : T, virtual Data {
    using T::T;
    /// Feeds Data buffer using T::readUpTo
    size_t available(size_t need) override;
};

struct URL {
    URL(){}
    /// Parses an absolute URL
    URL(string url);
    /// Parses \a url relative to this URL
    URL relative(URL&& url) const;
    explicit operator bool() const { return scheme || authorization || host || path || fragment || post; }

    String scheme, authorization, host, path, fragment, post;
};
String str(const URL& url);
inline bool operator ==(const URL& a, const URL& b) {
    return a.scheme==b.scheme&&a.authorization==b.authorization&&a.host==b.host&&a.path==b.path&&a.fragment==b.fragment&&a.post==b.post;
}
template<> inline URL copy(const URL& o) {
    URL url;
    url.scheme=copy(o.scheme); url.authorization=copy(o.authorization); url.host=copy(o.host); url.path=copy(o.path); url.fragment=copy(o.fragment); url.post=copy(o.post);
    return url;
}

/// Returns path to cache file for \a url
String cacheFile(const URL& url);

/// Requests ressource at \a url and call \a handler when available
/// \note Persistent disk caching will be used, no request will be sent if cache is younger than \a maximumAge hours
Map getURL(URL&& url, function<void(const URL&, Map&&)> handler={}, int maximumAge=14*24, bool wait=true);

/// Requests image at \a url and call \a handler when available (if was not cached)
void getImage(URL&& url, Image* target, function<void()> imageLoaded, int2 size=0, uint maximumAge=24*60);

