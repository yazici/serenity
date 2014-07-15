#pragma once
/// \file operation.h Abstract interface for results handled by a process manager
#include "array.h"
#include "map.h"
#include "string.h"
#include "file.h"
#include <typeinfo>
#include "data.h"

/// Abstract factory pattern (allows construction of class by names)
template <class I> struct Interface {
    struct AbstractFactory {
        /// Returns the version of this implementation
        virtual string version() abstract;
        virtual unique<I> constructNewInstance() abstract;
    };
    static map<string, AbstractFactory*>& factories() { static map<string, AbstractFactory*> factories; return factories; }
    template <class C> struct Factory : AbstractFactory {
        string version() override { return __DATE__ " " __TIME__ ""_; }
        unique<I> constructNewInstance() override { return unique<C>(); }
        Factory() { TextData s (str(typeid(C).name())); s.integer(); factories().insert(s.identifier(), this); }
        static Factory registerFactory;
    };
    static string version(const string& name) { return factories().at(name)->version(); }
    static unique<I> instance(const string& name) { return factories().at(name)->constructNewInstance(); }
};
template <class I> template <class C> typename Interface<I>::template Factory<C> Interface<I>::Factory<C>::registerFactory;

/// Dynamic-typed value
/// \note Implemented as a String with implicit conversions and copy
struct Variant : String {
    Variant(){}
    default_move(Variant);
    Variant(String&& s) : String(move(s)) {}
    Variant(double decimal) : String(ftoa(decimal)){}
    explicit operator bool() const { return size; }
    operator int() const { return *this ? fromInteger(*this) : 0; }
    operator uint() const { return *this ? fromInteger(*this) : 0; }
    operator float() const { return fromDecimal(*this); }
    operator double() const { return fromDecimal(*this); }
    generic operator T() const { return T((const string&)*this); } // Enables implicit conversion to any type with an implicit string constructor
};
template<> inline Variant copy(const Variant& o) { return copy((const String&)o); }
template<> inline String str(const Variant& o) { return copy(o); }
typedef map<String,Variant> Dict; /// Associative array of variants
inline Dict parseDict(TextData& s) {
    Dict dict;
    s.skip("{"_);
    for(;;) {
        if(s.match('}')) break;
        string key = s.whileNo(":|}"_);
        string value;
        if(s.match(':')) value = s.whileNo("|"_,'{','}');
        dict.insert(String(key), replace(String(value),'\\','/'));
        if(s.match('|')) continue;
        else if(s.match('}')) break;
        else error(s.untilEnd());
    }
    return dict;
}

/// Intermediate result
struct Result : shareable {
    Result(const string& name, long timestamp, Dict&& relevantArguments, String&& metadata, buffer<byte>&& data)
        : name(name), timestamp(timestamp), relevantArguments(move(relevantArguments)), metadata(move(metadata)), data(move(data)) {}
    String name; /// Unique identifier used to reference this result in rules' inputs and outputs
    int64 timestamp; /// Unix timestamp when this result finished succesfully computing [PersistentProcess: file last modified time]
    Dict relevantArguments; /// Relevant arguments (defined only by process and user)
    String metadata; /// Metadata (defined by Operation depending on inputs and arguments) [PersistentProcess: file system metadata (currently part of file name (limit to 255 bytes)]
    buffer<byte> data; /// Data for single outputs (generated by Operation depending on inputs and arguments) [PersistentProcess: file content and memory mapped when possible]
    map<String, buffer<byte>> elements; /// Data for map outputs (generated by Operation depending on inputs and arguments) [PersistentProcess: folder with one file for each element]
};
template<> inline String str(const Result& o) { return o.name+str(o.relevantArguments); }
