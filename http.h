#pragma once
#include "stream.h"
#include "process.h"
#include "function.h"
#include "file.h"

/// \a Socket is a network socket
struct Socket : virtual Stream {
    int fd=0;
    Socket(){}
    ~Socket() { disconnect(); }
    /// Connects to \a service on \a host
    bool connect(const ref<byte>& host, const ref<byte>& service);
    void disconnect();
    /// Reads /a size bytes from network socket
    virtual array<byte> receive(uint size);
    /// Writes /a buffer to network socket
    virtual void write(const ref<byte>& buffer);
    /// Stream
    uint available(uint need) override;
    ref<byte> get(uint size) const override;
};

typedef struct ssl_st SSL;
struct SSLSocket : Socket {
    ~SSLSocket();
    SSL* ssl=0;
    bool connect(const ref<byte>& host, const ref<byte>& service);
    array<byte> receive(uint size) override;
    void write(const ref<byte>& buffer) override;
};

/// Encodes \a input to Base64 to transfer binary data through text protocol
string base64(const string& input);

struct URL {
    string scheme,authorization,host,path,fragment;
    URL(){}
    URL(const ref<byte>& url);
    URL relative(URL&& url) const;
    explicit operator bool() { return host.size(); }
};
string str(const URL& url);
inline bool operator ==(const URL& a, const URL& b) {
    return a.scheme==b.scheme&&a.authorization==b.authorization&&a.host==b.host&&a.path==b.path&&a.fragment==b.fragment;
}

typedef function<void(const URL&, Map&&)> Handler;

struct HTTP : Poll, virtual TextStream, virtual SSLSocket {
    URL url;
    array<string> headers;
    ref<byte> method;
    array<string> redirect;
    uint contentLength=0;
    bool chunked=false;
    array<byte> content;
    Handler handler;

/// Connects to \a host and requests \a path using \a method.
/// \note \a headers and \a content will be added to request
/// \note If \a secure is true, an SSL connection will be used
/// \note HTTP should always be allocated on heap and no references should be taken.
    HTTP(const URL& url, Handler handler, array<string>&& headers =__(), const ref<byte>& method="GET"_);

   enum { Connect, Request, Header, Data, Cache, Handle, Done } state = Connect;
    void request();
    void header();
    void event() override;
};

/// Requests ressource at \a url and call \a handler when available
/// \note Persistent disk caching will be used, no request will be sent if cache is younger than \a maximumAge minutes
void getURL(const URL &url, Handler handler=[](const URL&, Map&&){}, int maximumAge=24*60);

/// Returns path to cache file for \a url
string cacheFile(const URL& url);

/// descriptor to cache folder
extern int cache;
