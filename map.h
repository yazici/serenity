#pragma once
#include "array.h"

template <class K, class V> struct const_pair { const K& key; const V& value; };
template <class K, class V> struct pair { K& key; V& value; };
template <class K, class V> struct map {
    map(){} move_only(map)
    int size() const { return keys.size; }
    bool contains(const K& key) const { return keys.contains(key); }
    explicit operator bool() const { return keys.size; }

    const V& at(const K& key) const { int i = keys.indexOf(key); assert(i>=0,"No matching key"_); return values[i];}
    V& at(const K& key) { int i = keys.indexOf(key); assert(i>=0,"No matching key"_); return values[i];}
    template<perfect(V)> Vf value(const K& key, Vf&& value) { int i = keys.indexOf(key); return i>=0 ? values[i] : forward<Vf>(value); }
    V* find(const K& key) { int i = keys.indexOf(key); return i>=0 ? addressof(values[i]) : 0; }
    template<perfect2(K,V)> V& insert(Kf&& key, Vf&& value) { keys << forward<Kf>(key); values << forward<Vf>(value); return values.last(); }
    template<perfect(K)> V& insert(Kf&& key) { insert(forward<Kf>(key),V()); return values.last(); }
    template<perfect(K)> V& operator [](Kf&& key) { int i = keys.indexOf(key); if(i>=0) return values[i]; return insert(forward<Kf>(key)); }
    void remove(const K& key) { int i=keys.indexOf(key); assert(i>=0); keys.removeAt(i); values.removeAt(i); }

    struct const_iterator {
        const K* k; const V* v;
        const_iterator(const K* k, const V* v) : k(k), v(v) {}
        bool operator!=(const const_iterator& o) const { return k != o.k; }
        const_pair<K,V> operator* () const { return i({*k,*v}); }
        const const_iterator& operator++ () { k++; v++; return *this; }
    };
    const_iterator begin() const { return const_iterator(keys.data,values.data); }
    const_iterator end() const { return const_iterator(&keys.data[keys.size],&values.data[values.size]); }

    struct iterator {
        K* k; V* v;
        iterator(K* k, V* v) : k(k), v(v) {}
        bool operator!=(const iterator& o) const { return k != o.k; }
        pair<K,V> operator* () const { return i({*k,*v}); }
        const iterator& operator++ () { k++; v++; return *this; }
    };
    iterator begin() { return iterator((K*)keys.data,(V*)values.data); }
    iterator end() { return iterator((K*)&keys.data[keys.size],(V*)&values.data[values.size]); }

    array<K> keys;
    array<V> values;
};

template<class K, class V> string str(const map<K,V>& m) {
    string s="{"_;
    for(int i=0;i<m.size();i++) { s<<str(m.keys[i],": "_,m.values[i]); if(i<m.size()-1) s<<", "_; }
    return s+"}"_;
}
