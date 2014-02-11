/// \file build.cc Builds C++ projects with automatic module dependency resolution
#include "thread.h"
#include "data.h"
#include "string.h"

struct Node {
    String name;
    array<Node*> children;
    Node(const string& name):name(copy(name)){}
};
bool operator ==(const Node& a, const Node& b) { return a.name==b.name; }
bool operator ==(const Node& a, const string& b) { return a.name==b; }

struct Build {
    string target = arguments().size>=1?arguments()[0]:"test"_;
    array<String> defines;
    array<string> flags;
    const Folder& folder = currentWorkingDirectory();
    const string tmp = "/var/tmp/"_;
    string CXX = existsFile("/usr/bin/clang++"_) ? "/usr/bin/clang++"_ : existsFile("/usr/bin/g++-4.8"_) ? "/usr/bin/g++-4.8"_ : "/usr/bin/g++"_;
    string LD = "/usr/bin/ld"_;
    int64 lastLinkEdit = 0;
    array<unique<Node>> modules;
    array<String> libraries;
    array<String> files;
    array<int> pids;

    array<String> sources = folder.list(Files|Recursive);
    /// Returns the first path matching file
    String find(const string& file) { for(String& path: sources) if(section(path,'/',-2,-1)==file) return String(path.contains('.')?section(path,'.',0,-2):path); return String(); }

    string tryParseIncludes(TextData& s) {
        if(!s.match("#include "_)) return ""_;
        if(s.match('"')) { // module header
            string name = s.until('.');
            return name;
        } else { // library header
            for(;s.peek()!='\n';s.advance(1)) if(s.match("//"_)) {
                string library=s.identifier("_"_);
                if(library) { assert(s.peek()=='\n',s.until('\n')); libraries += String(library); }
                break;
            }
            return ""_;
        }
    }
    void tryParseDefines(TextData& s) {
        if(!s.match("#define "_)) return;
        string id = s.identifier("_"_);
        s.whileAny(" "_);
        if(!s.match('0')) defines << toLower(id);
    }
    void tryParseConditions(TextData& s) {
        if(!s.match("#if "_)) return;
        bool condition = !s.match('!');
        string id = s.identifier("_"_);
        bool value = false;
        if(id=="1"_) value=true;
        else if(id=="__arm__"_ && flags.contains("arm"_)) value=true;
        else if(id=="__x86_64"_ && (!flags.contains("atom"_) && !flags.contains("arm"_))) value=true;
        else if(flags.contains(toLower(id))) value=true; // Conditionnal build (extern use flag)
        else if(defines.contains(toLower(id))) value=true; // Conditionnal build (intern use flag)
        if(value != condition) {
            for(; !s.match("#endif"_); s.line()) tryParseConditions(s);
        }
    }
    void tryParseFiles(TextData& s) {
        if(!s.match("FILE("_) && !s.match("ICON("_)) return;
        string name = s.identifier("_-"_);
        s.skip(")"_);

        String filesPath = tmp+"files"_+(flags.contains("arm"_)?".arm"_:flags.contains("atom"_)?".x32"_:".x64"_);
        Folder(filesPath, root(), true);
        String path = find(replace(name,"_"_,"/"_));
        assert(path, "No such file to embed", name);
        Folder subfolder = Folder(section(path,'/',0,-2), folder);
        string file = section(path,'/',-2,-1);
        String object = filesPath+"/"_+file+".o"_;
        assert_(!files.contains(object), name);
        int64 lastFileEdit = File(file, subfolder).modifiedTime();
        if(!existsFile(object) || lastFileEdit >= File(object).modifiedTime()) {
            if(execute(LD, split((flags.contains("atom"_)?"--oformat elf32-i386 "_:""_)+"-r -b binary -o"_)<<object<<file, true, subfolder))
                fail();
        }
        lastLinkEdit = max(lastLinkEdit, lastFileEdit);
        files << move(object);
    }

    /// Returns timestamp of the last modified interface header recursively parsing includes
    int64 parse(const string& name, Node& parent) {
        File file (name, folder);
        int64 lastEdit = file.modifiedTime();
        for(TextData s = file.read(file.size()); s; s.line()) {
            string name = tryParseIncludes(s);
            if(name) {
                String header = find(name+".h"_);
                if(header) lastEdit = max(lastEdit, parse(header+".h"_, parent));
                String module = find(name+".cc"_);
                if(!module || module == parent) continue;
                if(!modules.contains(module)) compileModule(module);
                parent.children << modules[modules.indexOf(module)].pointer;
            }
            tryParseDefines(s);
            tryParseConditions(s);
            tryParseFiles(s);
        }
        return lastEdit;
    }

    /// Compiles a module and its dependencies as needed
    /// \return Timestamp of the last modified module implementation (deep)
    void compileModule(const string& target) {
        assert(target);
        modules << unique<Node>(target);
        Node& module = modules.last();
        int64 lastEdit = parse(target+".cc"_, module);
        String object = tmp+join(flags,"-"_)+"/"_+target+".o"_;
        if(!existsFile(object, folder) || lastEdit >= File(object).modifiedTime()) {
            array<String> args;
            args << copy(object) << target+".cc"_;
            if(flags.contains("atom"_)) args << String("-m32"_) << String("-march=atom"_) << String("-mfpmath=sse"_);
            else if(flags.contains("arm"_)) args << String("-I/buildroot/output/host/usr/arm-buildroot-linux-uclibcgnueabihf/sysroot/usr/include/freetype2"_);
            else args << String("-march=native"_) << String("-I/usr/include/freetype2"_);
            if(!flags.contains("release"_)) args << String("-g"_);
            if(!flags.contains("debug"_)) args << String("-O3"_);
            if(flags.contains("profile"_)) args << String("-finstrument-functions"_)
                                                << String("-finstrument-functions-exclude-file-list=core,array,string,time,map,trace,profile"_);
            for(string flag: flags) args << "-D"_+toUpper(flag)+"=1"_;
            args << apply(folder.list(Folders), [this](const String& subfolder){ return "-iquote"_+subfolder; });
            log(target);
            while(pids.size>=1) { // Waits for a job to finish before launching a new unit
                int pid = wait(); // Waits for any child to terminate
                if(wait(pid)) fail();
                pids.remove(pid);
            }
            {static const array<string> flags = split("-c -pipe -std=c++11 -Wall -Wextra -o"_);
                pids << execute(CXX, flags+toRefs(args), false);}
        }
    }

    void fail() { log("Build failed"_); exit(-1); exit_thread(-1); }

    Build() {
        string install;
        for(string arg: arguments().slice(1)) if(startsWith(arg,"/"_)) install=arg; else flags << split(arg,'-');
        if(flags.contains("profile"_)) CXX="/usr/bin/g++"_; //FIXME: Clang does not support instrument-functions-exclude-file-list
        if(flags.contains("arm"_)) {
            CXX = "/buildroot/output/host/usr/bin/arm-buildroot-linux-uclibcgnueabihf-g++"_;
            LD = "/buildroot/output/host/usr/bin/arm-buildroot-linux-uclibcgnueabihf-ld"_;
        }

        Folder(tmp+join(flags,"-"_), root(), true);
        for(string subfolder: folder.list(Folders|Recursive)) Folder(tmp+join(flags,"-"_)+"/"_+subfolder, root(), true);
        compileModule( find(target+".cc"_) );
        if(flags.contains("profile"_)) compileModule(find("profile.cc"_));
        String binary = tmp+join(flags,"-"_)+"/"_+target+"."_+join(flags,"-"_);
        if(!existsFile(binary) || lastLinkEdit >= File(binary).modifiedTime()) {
            array<String> args; args<<String("-o"_)<<copy(binary);
            if(flags.contains("atom"_)) args<<String("-m32"_);
            args << apply(modules, [this](const unique<Node>& module){ return tmp+join(flags,"-"_)+"/"_+module->name+".o"_; });
            args << copy(files);
            args << apply(libraries, [this](const String& library){ return "-l"_+library; });
            for(int pid: pids) if(wait(pid)) fail(); // Wait for each translation unit to finish compiling before final linking
            if(execute(CXX, toRefs(args))) fail();
        }
        if(install && (!existsFile(target, install) || File(binary).modifiedTime() > File(target, install).modifiedTime())) rename(root(), binary, install, target);
    }
} build;
