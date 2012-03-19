#include "http.h"
#include "file.h"
#include <sys/socket.h>
#include <netdb.h>
#include <openssl/ssl.h>

/// Socket

void Socket::connect(const string& host, const string& service) {
    addrinfo* ai=0; getaddrinfo(strz(host).data(), strz(service).data(), 0, &ai);
    if(!ai) warn(service,"on"_,host,"not found"_);
    fd = socket(ai->ai_family,ai->ai_socktype,ai->ai_protocol);
    ::connect(fd, ai->ai_addr, ai->ai_addrlen);
    freeaddrinfo(ai);
}
Socket::~Socket() { if(fd) close( fd ); fd=0; }
uint Socket::available(uint need) {
    if(need==uint(-1)) buffer<<this->read(4096);
    else while(need>Buffer::available(need)) buffer<<this->read(need-Buffer::available(need));
    return Buffer::available(need);
}

/// SSLSocket

declare(static void ssl_init(), constructor) {
      SSL_library_init();
}
void SSLSocket::connect(const string& host, const string& service, bool secure) {
    Socket::connect(host,service);
    if(secure) {
        static SSL_CTX* ctx=SSL_CTX_new(TLSv1_client_method());
        ssl = SSL_new(ctx);
        SSL_set_fd(ssl,fd);
        SSL_connect(ssl);
    }
}
SSLSocket::~SSLSocket() { if(ssl) SSL_shutdown(ssl); }
array<byte> SSLSocket::read(int size) {
    array<byte> buffer(size);
    if(ssl) size=SSL_read(ssl, buffer.data(), size); else size=::read(fd,buffer.data(),size);
    buffer.setSize(size);
    return buffer;
}
void SSLSocket::write(const array<byte>& buffer) {
    if(ssl) SSL_write(ssl,buffer.data(),buffer.size());
    else ::write(fd,buffer.data(),buffer.size());
}

/// Base64

string base64(const string& input) {
    string output(input.size()*4/3+1);
    for(uint j=0;j<input.size();) {
        ubyte block[3];
        uint i=0; while(i<3 && j<input.size()) block[i++] = input[j++];
        assert(i);
        //encode 3 8-bit binary bytes as 4 '6-bit' characters
        const char cb64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        output
                << cb64[ block[0] >> 2 ]
                << cb64[ ((block[0] & 0x03) << 4) | ((block[1] & 0xf0) >> 4) ]
                << (i > 1 ? cb64[ ((block[1] & 0x0f) << 2) | ((block[2] & 0xc0) >> 6) ] : '=')
                << (i > 2 ? cb64[ block[2] & 0x3f ] : '=');
    }
    return output;
}

/// HTTP

HTTP::HTTP(string&& host, bool secure, string&& authorization) : host(move(host)), authorization(move(authorization)) {
    http.connect(this->host, secure?"https"_/*443*/:"http"_/*80*/, secure);
}
array<byte> HTTP::request(const string& path, const string& method, const string& post) {
    string request = method+" /"_+path+" HTTP/1.1\r\nHost: "_+host+"\r\n"_; //TODO: Accept-Encoding: gzip,deflate
    if(authorization) request << "Authorization: Basic "_+authorization+"\r\n"_;
    if(post) request << "Content-Length: "_+dec(post.size())+"\r\n"_;
    http.write( request+"\r\n"_+post );
    uint contentLength=0;
    if(http.match("HTTP/1.1 200 OK\r\n"_));
    else error(host+"/"_+path+":\n"_+http.until("\r\n\r\n"_));
    bool chunked=false;
    for(;;) { //parse header
        if(http.match("\r\n"_)) break;
        string key = http.until(": "_); string value=http.until("\r\n"_);
        if(key=="Content-Length"_) contentLength=toInteger(value);
        else if(key=="Transfer-Encoding"_ && value=="chunked"_) chunked=true;
    }
    if(contentLength) return http.DataStream::read<byte>(contentLength);
    else if(chunked) {
        array<byte> data;
        for(;;) {
            http.match("\r\n"_);
            int contentLength = toInteger(http.until("\r\n"_),16);
            if(contentLength == 0) return data;
            data << http.DataStream::read<byte>( contentLength );
        }
    } else error("Missing content",http.buffer);
}

array<byte> HTTP::get(const string& path) { return request(path,"GET"_,""_); }

array<byte> HTTP::post(const string& path, const string& content) { return request(path,"POST"_,content); }

array<byte> HTTP::getURL(const string& url) {
    TextBuffer s(copy(url));
    bool secure;
    if(s.match("http://"_)) secure=false;
    else if(s.match("https://"_)) secure=true;
    else error(url);
    string host = s.until("/"_);
    string path = s.readAll();

    static int cache=openFolder(strz(getenv("HOME"))+"/.cache"_);
    string name = replace(path,"/"_,"."_);
    string file = host+"/"_+name;
    if(exists(file,cache)) { //TODO: Expire, If-Modified
        return readFile(file,cache);
    } else {
        if(!exists(host,cache)) createFolder(host,cache);
        array<byte> content = HTTP(section(host,'@',-2,-1),secure,base64(section(host,'@'))).get(path);
        writeFile(file,content,cache);
        return move(content);
    }
}
