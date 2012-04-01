#pragma once
#include "string.h"
#include "map.h"
#include "stream.h"
#include <functional>

/// unique pointer to a heap-allocated value with move semantics (useful for recursive types)
template<class T> struct pointer {
    no_copy(pointer)
    pointer(T&& value):value(new T(move(value))){}
    pointer(pointer&& o) : value(o.value) { o.value=0; }
    //pointer& operator=(pointer&& o) { this->~pointer(); value=o.value; o.value=0; return *this; }
    ~pointer() { if(value) { value->~T(); value=0; } }
    T* value=0;
    const T& operator *() const { return *value; }
    //T& operator *() { return *value; }
    const T* operator ->() const { return value; }
    T* operator ->() { return value; }
    explicit operator bool() const { return value; }
    bool operator !() const { return !value; }
    operator const T*() const { return value; }
    //operator T*() { return value; }
};
template<class T> pointer<T> copy(const pointer<T>& p) { assert(p.value); return pointer<T>(copy(*p.value)); }
template<class T> string str(const pointer<T>& p) { assert(p.value); return str(*p.value); }

/// XML element
struct Element {
    string name, content;
    map< string, string > attributes;
    array< pointer<Element> > children;
    Element(){}
    //Element(Element&&)=default;
    Element(string&& content):content(move(content)){}
    Element(TextBuffer& s, bool html=false);
    explicit operator bool() { return name||content; }
    /// Collects text content of descendants
    string text() const;
    /// Collects text content of descendants matching path
    string text(const string& path) const;
    /// Returns value for \a attribute
    string operator[](const string& attribute) const;
    /// Returns child element with tag \a name
    Element operator()(const string& name) const;
    /// Depth-first visits all descendants
    void visit(const std::function<void(const Element&)>& visitor) const;
    /// process elements with matching \a path
    void xpath(const string& path, const std::function<void(const Element&)>& visitor) const;
    /// Tests if \a path match any elements
    bool match(const string& path) const;
    /// return value of \a attribute from elements with matching \a path
    //array<string> xpath(const string& path, const string& attribute) const;
    /// Returns element as parseable string
    string str(const string& prefix=""_) const;
};
template<> Element copy(const Element& e);
template<> string str(const Element& e);

/// Parse an XML document as a tree of \a Element
Element parseXML(array<byte>&& document);
/// Parse an HTML document as a tree of \a Element
Element parseHTML(array<byte>&& document);

/// Unescape XML entities
string unescape(const string& xml);
