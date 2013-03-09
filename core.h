#pragma once
/// \file core.h move/forward operators, predicate, integer types, IDE workarounds, variadic log, numeric range, std::initializer_list (aka ref), fat string and basic operations
// Attributes
#define unused __attribute((unused))
#define packed __attribute((packed))
#define Type typename
// Move
template<typename T> struct remove_reference { typedef T type; };
template<typename T> struct remove_reference<T&> { typedef T type; };
template<typename T> struct remove_reference<T&&> { typedef T type; };
#define remove_reference(T) typename remove_reference<T>::type
template<typename T> __attribute((always_inline)) constexpr remove_reference(T)&& move(T&& t) { return (remove_reference(T)&&)(t); }
template<typename T> void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
#define no_copy(T) T(const T&)=delete; T& operator=(const T&)=delete
#define move_operator_(T)                        T& operator=(T&& o){this->~T(); new (this) T(move(o)); return *this;} T(T&& o)
#define move_operator(T)       no_copy(T); T& operator=(T&& o){this->~T(); new (this) T(move(o)); return *this;} T(T&& o)
#define default_move(T) T(){} no_copy(T); T& operator=(T&& o){this->~T(); new (this) T(move(o)); return *this;} T(T&&)____(=default)
/// base template for explicit copy (overriden by explicitly copyable types)
template<typename T> T copy(const T& o) { return o; }

// Forward
template<typename> struct is_lvalue_reference { static constexpr bool value = false; };
template<typename T> struct is_lvalue_reference<T&> { static constexpr bool value = true; };
#define is_lvalue_reference(T) is_lvalue_reference<T>::value
template<typename T> constexpr T&& forward(remove_reference(T)& t) { return (T&&)t; }
template<typename T> constexpr T&& forward(remove_reference(T)&& t){ static_assert(!is_lvalue_reference(T),""); return (T&&)t; }

// Predicate
extern void* enabler;
template<bool> struct predicate {};
template<> struct predicate<true> { typedef void* type; };
#define predicate(E) typename predicate<E>::type& condition = enabler
#define predicate1(E) typename predicate<E>::type& condition1 = enabler

// Integer types
typedef char byte;
typedef signed char int8;
typedef unsigned char uint8;
typedef signed short int16;
typedef unsigned short uint16;
typedef unsigned short ushort;
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

// Works around missing support for some C++11 features in QtCreator code model
#ifndef __GXX_EXPERIMENTAL_CXX0X__
#define _ //string literal operator _""
#define __( args... ) //member initialization constructor {}
//#define ___ //variadic template arguments unpack operator ...
//#define ____( ignore... ) //=default, constructor{} initializer
#define ___ ...
#define ____( ignore... ) ignore
#define __thread
#else
#define __( args... ) { args }
#define ___ ...
#define ____( ignore... ) ignore
#endif

namespace std { template<typename T> struct initializer_list; }
/// \a Unmanaged const memory reference
template<typename T> using ref = std::initializer_list<T>;
/// Returns reference to string literals
inline constexpr ref<byte> operator "" _(const char* data, size_t size);

/// Logs to standard output
template<typename ___ Args> void log(const Args& ___ args);
template<> void log(const ref<byte>& message);
/// Logs to standard output and aborts immediatly this thread
template<typename ___ Args> void error(const Args& ___ args)  __attribute((noreturn));
template<> void error(const ref<byte>& message) __attribute((noreturn));

#ifdef DEBUG
#define warn error
/// Aborts if \a expr evaluates to false and logs \a expr and \a message
#define assert(expr, message...) ({ if(!(expr)) error(#expr ""_, ##message); })
#else
#define warn log
#define assert(expr, message...) ({})
#endif

/// Numeric range iterations
struct range {
    uint start,stop;
    range(uint start, uint stop):start(start),stop(stop){}
    range(uint size):range(0,size){}
    struct iterator {
        uint i;
        uint operator*() { return i; }
        iterator& operator++() { i++; return *this; }
        bool operator !=(const iterator& o) const{ return i<o.i; }
    };
    iterator begin(){ return __(start); }
    iterator end(){ return __(stop); }
};

// Memory operations
/// Raw zero initialization
inline void clear(byte* buffer, uint size) { for(uint i: range(size)) buffer[i]=0; }
/// Buffer default initialization
template<Type T> void clear(T* buffer, uint size, const T& value=T()) { for(uint i: range(size)) buffer[i]=value; }
/// Raw memory copy
inline void copy(byte* dst,const byte* src, uint size) { for(uint i: range(size)) dst[i]=src[i]; }
/// Buffer explicit copy
template<Type T> void copy(T* dst,const T* src, uint size) { for(uint i: range(size)) dst[i]=src[i]; }

// initializer_list
namespace std {
template<typename T> struct initializer_list {
    const T* data;
    uint size;
    constexpr initializer_list() : data(0), size(0) {}
    /// References \a size elements from read-only \a data pointer
    constexpr initializer_list(const T* data, uint size) : data(data), size(size) {}
    /// References elements sliced from \a begin to \a end
    constexpr initializer_list(const T* begin,const T* end) : data(begin), size(uint(end-begin)) {}
    /// References elements from a static array
    template<size_t N> initializer_list(const T (&a)[N]):ref<T>(a,N){}
    constexpr const T* begin() const { return data; }
    constexpr const T* end() const { return data+size; }
    const T& operator [](uint i) const { assert(i<size); return data[i]; }
    explicit operator bool() const { return size; }
    /// Compares all elements
    bool operator ==(const initializer_list<T>& o) const {
        if(size != o.size) return false;
        for(uint i: range(size)) if(!(data[i]==o.data[i])) return false;
        return true;
    }
    /// Slices a reference to elements from \a pos to \a pos + \a size
    initializer_list<T> slice(uint pos, uint size) const { assert(pos+size<=this->size); return initializer_list<T>(data+pos,size); }
    /// Slices a reference to elements from to the end of the reference
    initializer_list<T> slice(uint pos) const { assert(pos<=size); return initializer_list<T>(data+pos,size-pos); }
    /// Returns the index of the first occurence of \a value. Returns -1 if \a value could not be found.
    int indexOf(const T& key) const { for(uint i: range(size)) { if(data[i]==key) return i; } return -1; }
    /// Returns true if the array contains an occurrence of \a value
    bool contains(const T& key) const { return indexOf(key)>=0; }
    /// Copies elements to \a target and increments pointer
    void cat(T*& target) const { ::copy(target,data,size); target+=size; }
};
}

/// Returns reference to string literals
inline constexpr ref<byte> operator "" _(const char* data, size_t size) { return ref<byte>((byte*)data,size); }

/// References raw memory representation of \a t
template<Type T> ref<byte> raw(const T& t) { return ref<byte>((byte*)&t,sizeof(T)); }
/// Casts raw memory to \a T
template<Type T> const T& raw(const ref<byte>& a) { assert(a.size==sizeof(T)); return *(T*)a.data; }
/// Casts between element types
template<Type T, Type O> ref<T> cast(const ref<O>& o) {
    assert((o.size*sizeof(O))%sizeof(T) == 0);
    return ref<T>((const T*)o.data,o.size*sizeof(O)/sizeof(T));
}

// Basic operations
template<typename T> constexpr T min(T a, T b) { return a<b ? a : b; }
template<typename T> constexpr T max(T a, T b) { return a>b ? a : b; }
template<typename T> constexpr T clip(T min, T x, T max) { return x < min ? min : x > max ? max : x; }
template<typename T> constexpr T abs(T x) { return x>=0 ? x : -x; }
template<typename T> constexpr T sign(T x) { return x>=0 ? 1 : -1; }
template<typename A, typename B> bool operator !=(const A& a, const B& b) { return !(a==b); }
template<typename A, typename B> bool operator >(const A& a, const B& b) { return b<a; }

// Integer operations
/// Aligns \a offset to \a width (only for power of two \a width)
inline uint align(uint width, uint offset) { assert((width&(width-1))==0); return (offset + (width-1)) & ~(width-1); }

// Floating-point operations
inline float floor(float f) { return __builtin_floorf(f); }
inline float round(float f) { return __builtin_roundf(f); }
inline float ceil(float f) { return __builtin_ceilf(f); }
inline float fract(float f) { double one=1; return __builtin_modf(f, &one); }
inline float sqrt(float f) { return __builtin_sqrtf(f); }
inline float pow(float x, float y) { return __builtin_powf(x,y); }

// C runtime memory allocation
extern "C" void* malloc(size_t size);
extern "C" int posix_memalign(void** buffer, size_t alignment, size_t size);
extern "C" void* realloc(void* buffer, size_t size);
extern "C" void free(void* buffer);
// Typed memory allocation (without initialization)
template<Type T> T* allocate(uint size) { assert(size); return (T*)malloc(size*sizeof(T)); }
template<Type T> T* allocate64(uint size) { void* buffer; if(posix_memalign(&buffer,64,size*sizeof(T))) error(""); return (T*)buffer; }
template<Type T> void reallocate(T*& buffer, int need) { buffer=(T*)realloc((void*)buffer, need*sizeof(T)); }
template<Type T> void unallocate(T*& buffer) { assert(buffer); free((void*)buffer); buffer=0; }

/// Dynamic object allocation (using constructor and destructor)
inline void* operator new(size_t, void* p) { return p; } //placement new
template<Type T, Type... Args> T& heap(Args&&... args) { T* t=allocate<T>(1); new (t) T(forward<Args>(args)___); return *t; }
template<Type T> void free(T* t) { t->~T(); unallocate(t); }
