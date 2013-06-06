#pragma once
/// \file memory.h Memory operations and management (mref, buffer, unique, shared)
#include "core.h"

/// Unmanaged fixed-size mutable reference to an array of elements
template<Type T> struct mref : ref<T> {
    /// Default constructs an empty reference
    mref(){}
    /// References \a size elements from \a data pointer
    mref(T* data, uint64 size) : ref<T>(data,size){}

    T* begin() const { return (T*)data; }
    T* end() const { return (T*)data+size; }
    T& at(uint i) const { assert(i<size); return (T&)data[i]; }
    T& operator [](uint i) const { return at(i); }
    T& first() const { return at(0); }
    T& last() const { return at(size-1); }

    using ref<T>::data;
    using ref<T>::size;
};

// Memory operations
/// Initializes memory using a constructor (placement new)
inline void* operator new(size_t, void* p) { return p; }
/// Initializes raw memory to zero
inline void clear(byte* buffer, uint64 size) { for(uint i: range(size)) buffer[i]=0; }
/// Copies raw memory from \a src to \a dst
inline void copy(byte* dst, const byte* src, uint size) { for(uint i: range(size)) dst[i]=src[i]; }
/// Initializes buffer elements to \a value
template<Type T> void clear(T* buffer, uint64 size, const T& value=T()) { for(uint i: range(size)) new (&buffer[i]) T(copy(value)); }
/// Copies values from \a src to \a dst
/// \note Ignores move and copy operators
template<Type T> void rawCopy(T* dst,const T* src, uint size) { copy((byte*)dst, (const byte*)src, size*sizeof(T)); }

// C runtime memory allocation
extern "C" void* malloc(size_t size);
extern "C" int posix_memalign(void** buffer, size_t alignment, size_t size);
extern "C" void* realloc(void* buffer, size_t size);
extern "C" void free(void* buffer);

/// Managed fixed-capacity mutable reference to an array of elements
/// \note either an heap allocation managed by this object or a reference to memory managed by another object
/// \note Use array for objects with move constructors as buffer elements are not initialized on allocation
template<Type T> struct buffer : mref<T> {
    /// Default constructs an empty buffer
    buffer(){}
    /// References \a size elements from const \a data pointer
    buffer(const T* data, uint64 size) : mref<T>((T*)data, size) {}
    /// References \a o.size elements from \a o.data pointer
    explicit buffer(const ref<T>& o): mref<T>((T*)o.data, o.size) {}
    /// Move constructor
    buffer(buffer&& o) : mref<T>((T*)o.data, o.size), capacity(o.capacity) {o.data=0, o.size=0, o.capacity=0; }
    /// Allocates an uninitialized buffer for \a capacity elements
    buffer(uint64 capacity, uint64 size):mref<T>((T*)0,size),capacity(capacity){ assert(capacity>=size); if(!capacity) return; if(posix_memalign((void**)&data,64,capacity*sizeof(T))) error(""); }
    explicit buffer(uint64 size) : buffer(size, size){}
    /// Allocates a buffer for \a capacity elements and fill with value
    buffer(uint64 capacity, uint64 size, const T& value) : buffer(capacity, size) { clear((T*)data, size, value); }

    buffer& operator=(buffer&& o){ this->~buffer(); new (this) buffer(move(o)); return *this; }
    /// If the buffer owns the reference, returns the memory to the allocator
    ~buffer(){ if(capacity) ::free((void*)data); data=0; capacity=0; size=0; }

    // Overrides mref const operators
    T* begin() { return (T*)data; }
    T* end() { return (T*)data+size; }
    T& at(uint i) { assert(i<size); return (T&)data[i]; }
    T& operator [](uint i) { return at(i); }
    T& first() { return at(0); }
    T& last() { return at(size-1); }

    // and reenable const const versions
    const T* begin() const { return data; }
    const T* end() const { return data+size; }
    const T& at(uint i) const { assert(i<size); return data[i]; }
    const T& operator [](uint i) const { return at(i); }
    const T& first() const { return at(0); }
    const T& last() const { return at(size-1); }

    using mref<T>::data;
    using mref<T>::size;
    uint64 capacity=0; /// 0: reference, >0: size of the owned heap allocation
};
/// Initializes a new buffer with the content of \a o
template<Type T> buffer<T> copy(const buffer<T>& o){ buffer<T> t(o.capacity, o.size); for(uint i: range(o.size)) new (&t[i]) T(copy(o[i])); return t; }
/// Converts a reference to a buffer (unsafe as no reference counting will keep the original buffer from being freed)
template<Type T> buffer<T> unsafeReference(const ref<T>& o) { return buffer<T>(o.data, o.size); }

/// Unique reference to an heap allocated value
template<Type T> struct unique {
    struct null {};
    explicit unique(null):pointer(0){}
    template<Type D> unique(unique<D>&& o):pointer(dynamic_cast<T*>(o.pointer)){o.pointer=0;}
    template<Type... Args> explicit unique(Args&&... args):pointer(new (malloc(sizeof(T))) T(forward<Args>(args)...)){}
    unique& operator=(unique&& o){ this->~unique(); new (this) unique(move(o)); return *this; }
    ~unique() { if(pointer) { pointer->~T(); free(pointer); } pointer=0; }

    operator T&() { return *pointer; }
    operator const T&() const { return *pointer; }
    T* operator ->() { return pointer; }
    const T* operator ->() const { return pointer; }
    explicit operator bool() const { return pointer; }
    bool operator !() const { return !pointer; }
    bool operator ==(const unique<T>& o) const { return pointer==o.pointer; }

    T* pointer;
};
template<Type T> unique<T> copy(const unique<T>& o) { return unique<T>(copy(*o.pointer)); }

/// Reference to a shared heap allocated value managed using a reference counter
/// \note the shared type must implement a reference counter (e.g. by inheriting shareable)
/// \note Move semantics are still used whenever adequate (sharing is explicit)
template<Type T> struct shared {
    explicit shared():pointer(0){}
    template<Type D> shared(shared<D>&& o):pointer(dynamic_cast<T*>(o.pointer)){o.pointer=0;}
    template<Type... Args> explicit shared(Args&&... args):pointer(new (malloc(sizeof(T))) T(forward<Args>(args)...)){}
    shared& operator=(shared&& o){ this->~shared(); new (this) shared(move(o)); return *this; }
    explicit shared(const shared<T>& o):pointer(o.pointer){ pointer->addUser(); }
    ~shared() { if(!pointer) return; assert(pointer->userCount); if(pointer->removeUser()==0) { pointer->~T(); free(pointer); } pointer=0; }

    operator T&() { return *pointer; }
    operator const T&() const { return *pointer; }
    T* operator ->() { return pointer; }
    const T* operator ->() const { return pointer; }
    explicit operator bool() const { return pointer; }
    bool operator !() const { return !pointer; }
    bool operator ==(const shared<T>& o) const { return pointer==o.pointer; }

    T* pointer;
};
template<Type T> shared<T> copy(const shared<T>& o) { return shared<T>(copy(*o.pointer)); }
template<Type T> shared<T> share(const shared<T>& o) { return shared<T>(o); }
