#pragma once
#include "array.h"

/// Aligns \a offset to \a width (only for power of two \a width)
inline uint align(uint width, uint offset) { return (offset + (width - 1)) & ~(width - 1); }
/// Returns padding zeroes to append in order to align an array of \a size bytes to \a width
inline ref<byte> pad(uint width, uint size){ static byte zero[4]={}; assert_(width<=sizeof(zero)); return ref<byte>(zero,align(width,size)-size); }

/// References raw memory representation of \a t
template<class T> ref<byte> raw(const T& t) { return ref<byte>((byte*)&t,sizeof(T)); }
/// Casts raw memory to \a T
template<class T> const T& raw(const ref<byte>& a) { assert_(a.size==sizeof(T)); return *(T*)a.data; }
/// Casts between element types
template<class T, class O> ref<T> cast(const ref<O>& o) {
    assert_((o.size*sizeof(O))%sizeof(T) == 0);
    return ref<T>((const T*)o.data,o.size*sizeof(O)/sizeof(T));
}

/// \a Stream is an interface for readers (e.g \a DataStream, \a TextStream) implemented by sources (e.g \a Socket)
struct Stream {
    array<byte> buffer;
    uint index=0;

    Stream(){}
    Stream(Stream&& o):buffer(move(o.buffer)),index(o.index){}
    /// Creates a Stream interface to an \a array
    Stream(array<byte>&& array) : buffer(move(array)) {}
    /// Creates a Stream interface to a \a reference
    //explicit Stream(const ref<byte>& reference) : buffer(reference.data,reference.size) {} //TODO: escape analysis
//interface (default to buffer source)
    /// Returns number of bytes available, reading \a need bytes from underlying device if possible
    virtual uint available(uint /*need*/) { return buffer.size()-index; }
    /// Returns next \a size bytes from stream
    virtual ref<byte> get(uint size) { assert_(index+size<=buffer.size());  return ref<byte>(buffer.data()+index,size); } //TODO: escape analysis
    /// Advances \a count bytes in stream
    virtual void advance(int count) { index+=count;  assert_(index<=buffer.size()); }
//Stream helpers
    /// Returns the next byte in stream without advancing
    ubyte peek() const { assert_(index<buffer.size()); return buffer[index]; }
    /// Returns the next byte in stream and advance
    ubyte next() { assert_(index<buffer.size()); ubyte b=buffer[index]; advance(1); return b; }
    /// Reads \a size bytes from stream
    ref<byte> read(uint size) { ref<byte> t = get(size); advance(size); return t; }
    /// Slices an array referencing this data (valid as long as this stream)
    ref<byte> slice(int pos, int size) { return buffer.slice(pos,size); }
    /// Returns true if there is data to read
    explicit operator bool() { return available(1); }
};

#define swap32 __builtin_bswap32
inline uint16 __builtin_bswap16(uint16 x) { return (x<<8)|(x>>8); }
#define swap16 __builtin_bswap16

/// \a DataStream provides a convenient interface to parse binaries
struct DataStream : virtual Stream {
    bool isBigEndian = false;

    DataStream():Stream(){}
    DataStream(DataStream&& o):Stream(move(o)),isBigEndian(o.isBigEndian){}
    /// Creates a DataStream interface to an \a array
    DataStream(array<byte>&& array, bool isBigEndian=false) : Stream(move(array)), isBigEndian(isBigEndian) {}
    /// Creates a DataStream interface to a \a reference
    static DataStream byReference(const ref<byte>& reference, bool isBigEndian=false){
        DataStream s; s.isBigEndian=isBigEndian; s.buffer=array<byte>(reference.data,reference.size); return s;
    }
    DataStream& operator=(DataStream&& o){buffer=move(o.buffer);index=o.index;isBigEndian=o.isBigEndian;return *this;}

    /// Slices a stream referencing this data
    DataStream slice(int pos, int size) { return DataStream(array<byte>(buffer.data()+pos,size),isBigEndian); } //TODO: escape analysis
    /// Seeks stream to /a index
    void seek(uint index) { assert_(index<buffer.size()); this->index=index; }
    /// Seeks last match for \a key.
    bool seekLast(const ref<byte>& key);

    /// Reads from stream until next null byte
    ref<byte> untilNull();

    /// Reads one raw \a T element from stream
    template<class T> const T& read() { const T& t = raw<T>(Stream::read(sizeof(T))); return t; }

    /// Reads \a size raw \a T elements from stream
    template<class T>  ref<T> read(uint size) { return cast<T>(Stream::read(size*sizeof(T))); }

    /// Provides template overloaded specialization (for swap) and return type overloading through cast operators.
    struct ReadOperator {
        DataStream * s;
        // Swap here and not in read<T>() since functions that differ only in their return type cannot be overloaded
        /// Reads an int32 and if necessary, swaps from stream to host byte order
        operator uint32() { return s->isBigEndian?swap32(s->read<uint32>()):s->read<uint32>(); }
        operator int32() { return s->isBigEndian?swap32(s->read<int32>()):s->read<int32>(); }
        /// Reads an int16 and if necessary, swaps from stream to host byte order
        operator uint16() { return s->isBigEndian?swap16(s->read<uint16>()):s->read<uint16>(); }
        operator int16() { return s->isBigEndian?swap16(s->read<int16>()):s->read<int16>(); }
        /// Reads an int8
        operator uint8() { return s->read<uint8>(); }
        operator int8() { return s->read<int8>(); }
        operator byte() { return s->read<byte>(); }
        //template<class T> operator const T&(){ return s->read<T>(); }
    };
    ReadOperator read() { return __(this); }

    /// Provides return type overloading for reading arrays (swap as needed)
    struct ArrayReadOperator {
       DataStream* s; uint size;
       template<class T> operator array<T>() { array<T> t; for(uint i=0;i<size;i++) t<<(T)s->read(); return t; }
   };
   ArrayReadOperator read(uint size) { return __(this,size); }

   /// Reads \a size \a T elements from stream (swap as needed)
   template<class T>  void read(T buffer[], uint size) { for(uint i=0;i<size;i++) buffer[i]=(T)read(); }
};

/// \a TextStream provides a convenient interface to parse texts
struct TextStream : virtual Stream {
    TextStream():Stream(){}
    TextStream(TextStream&& o):Stream(move(o)){}
    /// Creates a Stream interface to an \a array
    TextStream(array<byte>&& array) : Stream(move(array)){}
    /// Creates a Stream interface to a \a reference
    static TextStream byReference(const ref<byte>& reference){TextStream s; s.buffer=array<byte>(reference.data,reference.size); return s;}
    TextStream& operator=(TextStream&& o){buffer=move(o.buffer);index=o.index;return *this;}

    /// If stream match \a key, advances \a pos by \a key size
    bool match(char key);
    /// If stream match \a key, advances \a pos by \a key size
    bool match(const ref<byte>& key);
    /// If stream match any of \a key, advances \a pos
    bool matchAny(const ref<byte>& any);
    /// If stream match none of \a key, advances \a pos
    bool matchNo(const ref<byte>& any);

    /// advances \a pos while stream doesn't match \a key
    /// \sa until
    ref<byte> whileNot(char key);
    /// advances \a pos while stream match any of \a key
    ref<byte> whileAny(const ref<byte>& key);
    /// advances \a pos while stream match none of \a key
    ref<byte> whileNo(const ref<byte>& key);

    /// Reads until stream match \a key
    /// \sa whileNot
    ref<byte> until(char key);
    /// Reads until stream match \a key
    ref<byte> until(const ref<byte>& key);
    /// Reads until stream match any character of \a key
    ref<byte> untilAny(const ref<byte>& any);
    /// Reads until the end of stream
    ref<byte> untilEnd();
    /// Skips whitespaces
    void skip();
    /// Reads a single word
    ref<byte> word();
    /// Reads a single identifier [a-zA-Z0-9_-:]*
    ref<byte> identifier();
    /// Reads a single number
    int number(int base=10);
    /// Reads one possibly escaped character
    char character();
};
