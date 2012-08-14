#include "file.h"
#include "linux.h"
#include "debug.h"

/// Input/Output

array<byte> read(int fd, uint capacity) {
    array<byte> buffer(capacity);
    int size = check( read(fd,buffer.data(),capacity) );
    assert((uint)size==capacity,size);
    buffer.setSize(size);
    return buffer;
}
array<byte> readUpTo(int fd, uint capacity) {
    array<byte> buffer(capacity);
    int size = check( read(fd,buffer.data(),capacity) );
    buffer.setSize(size);
    return buffer;
}

/// File

File::~File() { if(fd>0) close(fd); }

bool exists(const ref<byte>& file, int at) { return File( openat(at, strz(file), O_RDONLY, 0) ).fd > 0; }

File openFile(const ref<byte>& file, int at) {
    return File( check( openat(at, strz(file), O_RDONLY, 0), file) );
}
File createFile(const ref<byte>& file, int at, bool overwrite) {
    assert(overwrite || !exists(file,at), file);
    return File( check( openat(at, strz(file),O_CREAT|O_WRONLY|O_TRUNC,0666), file ) );
}
File appendFile(const ref<byte>& file, int at) {
    return File( check( openat(at, strz(file),O_CREAT|O_RDWR|O_APPEND,0666), file ) );
}

array<byte> readFile(const ref<byte>& file, int at) {
    File fd = openFile(file,at);
    struct stat sb; fstat(fd, &sb);
    array<byte> content = read(fd,sb.size);
    debug( if(content.size()>1<<20) { trace(); log("use mapFile to avoid copying "_+dec(content.size()>>10)+"KB"_); } )
    return content;
}

void writeFile(const ref<byte>& file, const ref<byte>& content, int at, bool overwrite) {
    File fd = createFile(file,at,overwrite);
    uint unused wrote = write(fd,content.data,content.size);
    assert(wrote==content.size);
}

Map mapFile(const ref<byte>& file, int at) { File fd=openFile(file,at); Map map=mapFile(fd); return map; }
Map mapFile(int fd) {
    struct stat sb; fstat(fd, &sb);
    if(sb.size==0) return Map(0,0);
    const byte* data = (byte*)mmap(0,sb.size,PROT_READ,MAP_PRIVATE,fd,0);
    assert(data);
    return Map(data,(int)sb.size);
}
Map::~Map() { if(data) munmap((void*)data,size); }

/// File system

int root() { static int fd = openFolder("/"_,-100); return fd; }

int openFolder(const ref<byte>& folder, int at) { return check( openat(at, strz(folder), O_RDONLY|O_DIRECTORY, 0), folder ); }

void createFolder(const ref<byte>& folder, int at) { int unused e= check( mkdirat(at, strz(folder), 0666), folder); }

void symlink(const ref<byte>& target,const ref<byte>& name, int at) {
    assert(target!=name);
    unlinkat(at,strz(name),0);
    int unused e= check(symlinkat(strz(target),at,strz(name)), name,"->",target);
}

stat statFile(const ref<byte>& path, int at) { File fd = openFile(path,at); stat file; int unused e= check( fstat(fd, &file) ); return file; }
enum { S_IFDIR=0040000 };
bool isFolder(const ref<byte>& path, int at) { return statFile(path,at).mode&S_IFDIR; }
long modifiedTime(const ref<byte>& path, int at) { return statFile(path,at).mtime.sec; }

/*array<string> listFiles(const ref<byte>& folder, Flags flags, int at) {
    int fd = openFolder(folder,at);
    assert(fd, "Folder not found"_, folder);
    array<string> list;
    int i=0; for(dirent entry; getdents(fd,&entry,sizeof(entry))>0;i++) { if(i<2) continue;
        string name = str(entry.name);
        string path = folder+"/"_+name;
        int type = *((byte*)&entry + entry.len - 1);
        if(type==DT_DIR && flags&Recursive) {
            if(flags&Sort) for(string& e: listFiles(path,flags,at)) insertSorted(list, move(e));
            else list << move(listFiles(path,flags,at));
        } else if((type==DT_DIR && flags&Folders) || (type==DT_REG && flags&Files)) {
            if(flags&Sort) insertSorted(list, move(path));
            else list << move(path);
        }
    }
    close(fd);
    return list;
}

string findFile(const ref<byte>& folder, const ref<byte>& file, int at) {
    int fd = openFolder(folder,at);
    assert(fd, "Folder not found"_, folder);
    int i=0; for(dirent entry; getdents(fd,&entry,sizeof(entry))>0;i++) { if(i<2) continue;
        string name = str(entry.name);
        int type = *((byte*)&entry + entry.len - 1);
        if(type==DT_DIR) { string path=findFile(name,file,fd); if(path) return folder+"/"_+path; }
        else if(type==DT_REG && file==name) return folder+"/"_+name;
    }
    close(fd);
    return ""_;
}*/
