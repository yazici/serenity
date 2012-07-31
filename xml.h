#pragma once
#include "string.h"
#include "map.h"
#include "stream.h"
#include "function.h"
#include "memory.h"

/// XML element
struct Element {
    string name, content;
    map< string, string > attributes;
    array< unique<Element> > children;
    Element(){}
    Element(string&& content):content(move(content)){}
    Element(TextStream& s, bool html=false);
    explicit operator bool() { return name||content; }
    /// Returns value for \a attribute (fail if missing)
    ref<byte> at(const ref<byte>& attribute) const;
    /// Returns value for \a attribute (empty string if missing)
    ref<byte> operator[](const ref<byte>& attribute) const;
    /// Returns child element with tag \a name (beware of dangling reference)
    const Element& operator()(const ref<byte>& name) const;
    /// Depth-first visits all descendants
    void visit(const function<void(const Element&)>& visitor) const;
    /// Depth-first visits all descendants, skip element if /a visitor returns false.
    void mayVisit(const function<bool(const Element&)>& visitor) const;
    /// Process elements with matching \a path
    void xpath(const ref<byte>& path, const function<void(const Element&)>& visitor) const;
    /// Collects text content of descendants
    string text() const;
    /// Collects text content of descendants matching path
    string text(const ref<byte>& path) const;
    /// Returns element as parseable string
    string str(const ref<byte>& prefix=""_) const;
};
string str(const Element& e);

/// Parse an XML document as a tree of \a Element
Element parseXML(array<byte>&& document);
/// Parse an HTML document as a tree of \a Element
Element parseHTML(array<byte>&& document);

/// Unescape XML entities
string unescape(const ref<byte>& xml);
