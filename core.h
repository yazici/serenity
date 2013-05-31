#pragma once
/// \file core.h keywords, basic types, debugging, ranges

// Keywords
#define unused __attribute((unused))
#define packed __attribute((packed))
/// Less verbose template declarations
#define Type typename
/// Nicer abstract declaration
#define abstract =0
/// Declares the default move constructor and move assigmment operator
#define default_move(T) T(T&&)=default; T& operator=(T&&)=default

// Move semantics
template<Type T> struct remove_reference { typedef T type; };
template<Type T> struct remove_reference<T&> { typedef T type; };
template<Type T> struct remove_reference<T&&> { typedef T type; };
/// Allows move assignment
template<Type T> inline constexpr Type remove_reference<T>::type&& move(T&& t) { return (Type remove_reference<T>::type&&)(t); }
/// Swap values (using move semantics as necessary)
template<Type T> void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
/// Base template for explicit copy (overriden by explicitly copyable types)
template<Type T> T copy(const T& o) { return o; }

// Forward
template<Type> struct is_lvalue_reference { static constexpr bool value = false; };
template<Type T> struct is_lvalue_reference<T&> { static constexpr bool value = true; };
/// Forwards references and copyable values
template<Type T> constexpr T&& forward(Type remove_reference<T>::type& t) { return (T&&)t; }
/// Forwards moveable values
template<Type T> constexpr T&& forward(Type remove_reference<T>::type&& t){static_assert(!is_lvalue_reference<T>::value,""); return (T&&)t; }

// Comparison functions
template<Type A, Type B> bool operator !=(const A& a, const B& b) { return !(a==b); }
template<Type A, Type B> bool operator >(const A& a, const B& b) { return b<a; }
template<Type T> T min(T a, T b) { return a<b ? a : b; }
template<Type T> T max(T a, T b) { return a>b ? a : b; }
template<Type T> T clip(T min, T x, T max) { return x < min ? min : x > max ? max : x; }
template<Type T> T abs(T x) { return x>=0 ? x : -x; }

// Basic types
typedef char byte;
typedef signed char int8;
typedef unsigned char uint8;
typedef signed short int16;
typedef unsigned short uint16;
typedef signed int int32;
typedef unsigned int uint32;
typedef unsigned int uint;
typedef unsigned long ptr;
typedef signed long long int64;
typedef unsigned long long uint64;
#if __x86_64
typedef unsigned long size_t;
#else
typedef unsigned int size_t;
#endif
namespace std { template<Type T> struct initializer_list { const T* data; size_t size; }; }
template<Type T> struct ref;
inline constexpr ref<byte> operator "" _(const char* data, size_t size);
#ifndef __GXX_EXPERIMENTAL_CXX0X__
#define _ // QtCreator doesn't parse custom literal operators (""_)
#endif

// Debugging
/// Logs a message to standard output
template<Type... Args> void log(const Args&... args);
template<> void log(const ref<byte>& message);
/// Logs a message to standard output and signals all threads to log their stack trace and abort
template<Type... Args> void error(const Args&... args)  __attribute((noreturn));
template<> void error(const ref<byte>& message) __attribute((noreturn));

#ifdef DEBUG
#define warn error
/// Aborts if \a expr evaluates to false and logs \a expr and \a message
#define assert(expr, message...) ({ if(!(expr)) error(#expr ""_, ##message); })
#else
#define warn log
#define assert(expr, message...) ({})
#endif
/// Aborts if \a expr evaluates to false and logs \a expr and \a message (even in release)
#define assert_(expr, message...) ({ if(!(expr)) error(#expr ""_, ##message); })

/// Numeric range
struct range {
    range(uint start, uint stop) : start(start), stop(stop){}
    range(uint size) : range(0, size){}
    struct iterator {
        uint i;
        uint operator*() { return i; }
        iterator& operator++() { i++; return *this; }
        bool operator !=(const iterator& o) const{ return i<o.i; }
    };
    iterator begin(){ return {start}; }
    iterator end(){ return {stop}; }
    uint start, stop;
};

/// Const memory range
template<Type T> struct ref {
    /// Default constructs an empty reference
    constexpr ref() : data(0), size(0) {}
    /// References \a size elements from const \a data pointer
    constexpr ref(const T* data, uint64 size) : data(data), size(size) {}
    /// References \a size elements from const \a data pointer
    constexpr ref(const T* begin, const T* end) : data(begin), size(end-begin) {}
    /// Converts an std::initializer_list to ref
    constexpr ref(const std::initializer_list<T>& list) : data(list.data), size(list.size) {}
    /// Converts a static array to ref
    template<size_t N> ref(const T (&a)[N]):  ref(a,N) {}

    const T* begin() const { return data; }
    const T* end() const { return data+size; }
    const T& operator [](uint i) const { assert(i<size); return data[i]; }
    explicit operator bool() const { return size; }

    /// Slices a reference to elements from \a pos to \a pos + \a size
    ref<T> slice(uint pos, uint size) const { assert(pos+size<=this->size); return ref<T>(data+pos,size); }
    /// Slices a reference to elements from to the end of the reference
    ref<T> slice(uint pos) const { assert(pos<=size); return ref<T>(data+pos,size-pos); }

    /// Compares all elements
    bool operator ==(const ref<T>& o) const {
        if(size != o.size) return false;
        for(uint i: range(size)) if(data[i]!=o.data[i]) return false;
        return true;
    }
    /// Returns the index of the first occurence of \a value. Returns -1 if \a value could not be found.
    int indexOf(const T& key) const { for(uint i: range(size)) { if(data[i]==key) return i; } return -1; }
    /// Returns true if the array contains an occurrence of \a value
    bool contains(const T& key) const { return indexOf(key)>=0; }

    const T* data;
    uint64 size;
};

/// Mutable memory range
template<Type T> struct memory {
    /// References \a size elements from const \a data pointer
    constexpr memory(T* data, uint64 size) : data(data), size(size) {}
    T& operator [](uint i) { assert(i<size, i, size); return data[i]; }
    T* data;
    uint64 size;
};

/// Returns const reference to a static string literal
inline constexpr ref<byte> operator "" _(const char* data, size_t size) { return ref<byte>(data,size); }
/// Returns const reference to memory used by \a t
template<Type T> ref<byte> raw(const T& t) { return ref<byte>((byte*)&t,sizeof(T)); }

/// Declares a file to be embedded in the binary
#define FILE(name) static ref<byte> name() { \
    extern char _binary_ ## name ##_start[], _binary_ ## name ##_end[]; \
    return ref<byte>(_binary_ ## name ##_start,_binary_ ## name ##_end); \
}

// Integer operations
/// Aligns \a offset down to previous \a width wide step (only for power of two \a width)
inline uint floor(uint width, uint offset) { assert((width&(width-1))==0); return offset & ~(width-1); }
/// Aligns \a offset to \a width (only for power of two \a width)
inline uint align(uint width, uint offset) { assert((width&(width-1))==0); return (offset + (width-1)) & ~(width-1); }
/// Returns floor(log2(v))
inline uint log2(uint v) { uint r=0; while(v >>= 1) r++; return r; }
/// Computes the next highest power of 2
inline uint nextPowerOfTwo(uint v) { v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; v++; return v; }
/// Returns the largest positive integer that divides the numbers without a remainder
inline int gcd(uint a, uint b) { while(b != 0) { int t = b; b = a % b; a = t; } return a; }
/// Simplify a fraction by its greatest common divisor
inline void simplify(uint& a, uint& b) { int divisor = gcd(a,b); a/=divisor; b/=divisor; }

// Memory operations
// Initializes memory using a constructor (placement new)
inline void* operator new(size_t, void* p) { return p; }
/// Initializes raw memory to zero
inline void clear(byte* buffer, uint64 size) { for(uint i: range(size)) buffer[i]=0; }
/// Initializes memory to \a value
template<Type T> void clear(T* buffer, uint64 size, const T& value=T()) { for(uint i: range(size)) new (&buffer[i]) T(copy(value)); }
/// Copies values from \a src to \dst
template<Type T> void copy(T* dst,const T* src, uint size) { for(uint i: range(size)) dst[i]=src[i]; }

// C runtime memory allocation
extern "C" void* malloc(size_t size);
extern "C" int posix_memalign(void** buffer, size_t alignment, size_t size);
extern "C" void* realloc(void* buffer, size_t size);
extern "C" void free(void* buffer);

/// Reference type with move semantics
template<Type T> struct handle {
    handle(T pointer=T()):pointer(pointer){}
    handle& operator=(handle&& o){ pointer=o.pointer; o.pointer=0; return *this; }
    handle(handle&& o):pointer(o.pointer){o.pointer=T();}

    operator T() const { return pointer; }
    operator T&() { return pointer; }
    T* operator &() { return &pointer; }
    T operator ->() { return pointer; }

    T pointer;
};

/// Reference to a fixed capacity memory region (either an heap allocation managed by this object or a reference to memory managed by another object)
template<Type T> struct buffer {
    /// Default constructs an empty buffer
    buffer(){}
    //buffer(T* data, uint64 capacity, uint64 size):data(data),capacity(capacity),size(size){}
    /// References \a size elements from \a data pointer
    buffer(T* data, uint64 size):data(data),size(size){}
    /// References \a o.size elements from \a o.data pointer
    explicit buffer(const ref<T>& o):data((T*)o.data),capacity(0),size(o.size){}
    buffer(buffer&& o):data(o.data),capacity(o.capacity),size(o.size){o.data=0, o.size=0, o.capacity=0;}
    /// Allocates an uninitialized buffer for \a capacity elements
    buffer(uint64 capacity, uint64 size):capacity(capacity),size(size){ assert(capacity>=size); if(!capacity) return; if(posix_memalign((void**)&data,64,capacity*sizeof(T))) error(""); }
    explicit buffer(uint64 size):buffer(size,size){}
    /// Allocates a buffer for \a capacity elements and fill with value
    buffer(uint64 capacity, uint64 size, const T& value):buffer(capacity,size){clear(data,size,value);}

    buffer& operator=(buffer&& o){ this->~buffer(); new (this) buffer(move(o)); return *this; }
    /// If the buffer owns the reference, returns the memory to the allocator
    ~buffer(){ if(capacity) ::free(data); data=0; capacity=0; size=0; }

    /// Returns true if not empty
    explicit operator bool() const { return size; }
    operator const T*() const { return data; }
    /// Returns a const reference to this buffer
    operator ref<T>() const { return ref<T>(data,size); }

    /// \name Iterators
    const T* begin() const { return data; }
    const T* end() const { return data+size; }
    T* begin() { return data; }
    T* end() { return data+size; }
    /// \}

    /// Slices a const reference to elements from \a pos to \a pos + \a size
    ref<T> slice(uint pos, uint size) const { assert(pos+size<=this->size); return ref<T>(data+pos,size); }
    /// Slices a const reference to elements from \a pos the end of the array
    ref<T> slice(uint pos) const { assert(pos<=size); return ref<T>(data+pos,size-pos); }

    /// \name Accessors
    const T& at(uint i) const { assert(i<size,i,size); return data[i]; }
    T& at(uint i) { assert(i<size,i,size); return data[i]; }
    const T& operator [](uint i) const { return at(i); }
    T& operator [](uint i) { return at(i); }
    const T& first() const { return at(0); }
    T& first() { return at(0); }
    const T& last() const { return at(size-1); }
    T& last() { return at(size-1); }
    /// \}

    /// Slices a mutable reference to elements from \a pos to \a pos + \a size
    //memory<T> slice(uint64 pos, uint64 size) { assert(pos+size<=this->size); return memory<T>(data+pos, data+pos+size); }
    /// Slices a mutable reference to elements from \a pos the end of the array
    //memory<T> slice(uint64 pos) { assert(pos<=size); return memory<T>(data+pos, data+size); }

    T* data=0; /// Pointer to the buffer valid while not reallocated
    uint64 capacity=0; /// 0: reference, >0: size of the owned heap allocation
    uint64 size=0; /// Number of elements currently in this buffer
};
template<Type T> buffer<T> copy(const buffer<T>& o){ buffer<T> t(o.capacity, o.size); copy(t.data,o.data,o.size); return t; }

/// Unique reference to an heap allocated value
template<Type T> struct unique {
    struct null {};
    explicit unique(null):pointer(0){}
    //unique(unique&& o):pointer(o.pointer){o.pointer=0;}
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
