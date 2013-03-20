#pragma once
/// \file file.h Unix stream I/O and file system abstraction (Handle, Folder, Stream, Socket, File, Device, Map)
#include "array.h"

/// Unix file descriptor
struct Handle {
    handle<int> fd;
    Handle(int fd):fd(fd){}
    default_move(Handle);
    ~Handle();
    operator bool() const { return fd; }
};

struct Folder;
/// Returns a file descriptor to the current working directory
const Folder& cwd();
/// Returns a file descriptor to the root folder
const Folder& root();

enum { Files=1<<0, Folders=1<<1, Recursive=1<<2 };
struct Folder : Handle {
    /// Opens \a folder
    Folder(const ref<byte>& folder, const Folder& at=root(), bool create=false);
    /// Lists all files in \a folder
    array<string> list(uint flags);
};
/// Returns whether this \a folder exists (as a folder)
bool existsFolder(const ref<byte>& folder, const Folder& at=root());

enum { IDLE=64 };
#include <poll.h>

/// Handle to an Unix I/O stream
struct Stream : Handle {
    Stream(int fd):Handle(fd){}
    /// Reads exactly \a size bytes into \a buffer
    void read(void* buffer, uint size);
    /// Reads up to \a size bytes into \a buffer
    int readUpTo(void* buffer, uint size);
    /// Reads exactly \a size bytes
    array<byte> read(uint size);
    /// Reads up to \a size bytes
    array<byte> readUpTo(uint size);
    /// Reads a raw value
    template<Type T> T read() { T t; read((byte*)&t,sizeof(T)); return t; }
    /// Reads \a size raw values
    template<Type T> array<T> read(uint size) {
        array<T> buffer(size,size); uint byteSize=size*sizeof(T);
        for(uint i=0;i<byteSize;) i+=readUpTo(buffer.data+i, byteSize-i);
        return buffer;
    }
    /// Polls whether reading would block
    bool poll(int timeout=0);
    /// Writes \a buffer of \a size bytes
    void write(const byte* data, uint size);
    /// Writes \a buffer
    void write(const ref<byte>& buffer);
};

/// Handle to a socket
struct Socket : Stream {
    enum {PF_LOCAL=1, PF_INET};
    enum {SOCK_STREAM=1, SOCK_DGRAM, O_NONBLOCK=04000};
    Socket(int domain, int type);
};

enum {ReadOnly, WriteOnly, ReadWrite, Create=0100, Truncate=01000, Append=02000, Asynchronous=020000};
/// Handle to a file
struct File : Stream {
    File(int fd):Stream(fd){}
    /// Opens \a path
    /// If read only, fails if not existing
    /// If write only, fails if existing
    File(const ref<byte>& path, const Folder& at=root(), uint flags=ReadOnly);
    /// Returns file size
    int size() const;
    /// Seeks to \a index
    void seek(int index);
};
/// Returns whether \a path exists (as a file)
bool existsFile(const ref<byte>& path, const Folder& at=root());
/// Reads whole \a file content
array<byte> readFile(const ref<byte>& path, const Folder& at=root());
/// Writes \a content into \a file (overwrites any existing file)
void writeFile(const ref<byte>& path, const ref<byte>& content, const Folder& at=root());

template<uint major, uint minor> struct IO { static constexpr uint io = major<<8 | minor; };
template<uint major, uint minor, Type T> struct IOW { typedef T Args; static constexpr uint iow = 1<<30 | sizeof(T)<<16 | major<<8 | minor; };
template<uint major, uint minor, Type T> struct IOR { typedef T Args; static constexpr uint ior = 2<<30 | sizeof(T)<<16 | major<<8 | minor; };
template<uint major, uint minor, Type T> struct IOWR { typedef T Args; static constexpr uint iowr = 3u<<30 | sizeof(T)<<16 | major<<8 | minor; };
/// Handle to a device
struct Device : File {
    Device(const ref<byte>& path, int flags=ReadWrite):File(path, root(), flags){}
    /// Sends ioctl \a request with untyped \a arguments
    int ioctl(uint request, void* arguments);
    /// Sends ioctl request with neither input/outputs arguments
    template<Type IO> int io() { return ioctl(IO::io, 0); }
    /// Sends ioctl request with \a input arguments
    template<Type IOW> int iow(const typename IOW::Args& input) { return ioctl(IOW::iow, &input); }
    /// Sends ioctl request with output arguments
    template<Type IOR> typename IOR::Args ior() { typename IOR::Args output; ioctl(IOR::ior, &output); return output; }
    /// Sends ioctl request with \a reference argument
    template<Type IOWR> int iowr(typename IOWR::Args& reference) { return ioctl(IOWR::iowr, &reference); }
};

/// Managed memory mapping
struct Map {
    enum {Read=1, Write=2};
    enum {Shared=1, Private=2, Anonymous=32};

    Map(){}
    Map(const File& file, uint prot=Read);
    Map(const ref<byte>& path, const Folder& at=root(), uint prot=Read):Map(File(path,at),prot){}
    Map(uint fd, uint offset, uint size, uint prot, uint flags=Shared);
    default_move(Map);
    ~Map();

    operator ref<byte>() { return ref<byte>(data, size); }

    /// Locks memory map in RAM
    void lock(uint size=-1) const;

    handle<byte*> data;
    uint size=0;
};

/// Creates a symbolic link to \a target at \a name, replacing any existing files or links
void symlink(const ref<byte>& target,const ref<byte>& name, const Folder& at=root());
/// Returns the last modified time for \a path
long modifiedTime(const ref<byte>& path, const Folder& at=root());
/// Sets the last modified time for \a path to current time
void touchFile(const ref<byte>& path, const Folder& at=root());
