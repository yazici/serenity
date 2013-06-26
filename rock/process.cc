#include "process.h"
#include "data.h"
#include "time.h"
#include "math.h"

/// Relevant operation parameters
array<string> Rule::parameters() const {
    array<string> parameters;
    if(operation) {
        unique<Operation> operation = Interface<Operation>::instance(this->operation);
        assert_(operation, "Operation", this->operation, "not found in", Interface<Operation>::factories.keys);
        parameters += split(operation->parameters());
    }
    return parameters;
}

array<string> Process::parameters() {
    array<string> parameters = copy(specialParameters);
    for(auto factory: Interface<Operation>::factories.values) parameters += split(factory->constructNewInstance()->parameters());
    return parameters;
}

array<string> Process::configure(const ref<string>& allArguments, const string& definition) {
    array<string> targets;
    array<array<string>> results; // Intermediate result names for each target
    Dict defaultArguments; // Process-specified default arguments
    // Parses definitions and arguments twice to solve cyclic dependencies
    // 1) Process definition defines valid parameters and targets
    // 2) Arguments are parsed using default definition
    // 3) Process definition is parsed with conditionnals taken according to user arguments
    // 4) Arguments are parsed again using the customized process definition
    for(uint pass unused: range(2)) {
        array<string> parameters = this->parameters();
        rules.clear(); resultNames.clear(); defaultArguments.clear();

        for(TextData s(definition); s;) { //FIXME: use parser generator
            s.skip();
            if(s.match('#')) { s.until('\n'); continue; }
            if(s.match("if"_)) {
                s.whileAny(" \t\r"_);
                bool enable;
                string op;
                string right;
                if(s.match('!')) op="=="_, right="0"_;
                string parameter = s.word("_-."_);
                parameters += parameter;
                String left ("0"_);
                if(arguments.contains(parameter)) left = copy(arguments.at(parameter));
                if(!left) left=String("1"_);
                s.whileAny(" \t\r"_);
                if(!op) op = s.whileAny("!="_);
                if(!op) op = "!="_, right = "0"_;
                if(!right) { s.whileAny(" \t\r"_); s.skip("'"_); right = s.until('\''); }
                assert_(left && right);
                if(op=="=="_) enable = (left == right);
                else if(op=="!="_) enable = (left != right);
                else error("Unknown operator", op);
                s.skip(":"_);
                if(!enable) { s.until('\n'); continue; }
                s.whileAny(" \t\r"_);
            }
            array<string> outputs;
            for(;!s.match('='); s.whileAny(" \t\r"_)) {
                string output = s.word("_-."_);
                assert_(output, s.until('\n'));
                outputs << output;
            }
            assert_(outputs, s.until('\n'));
            s.whileAny(" \t\r"_);
            if(outputs.size==1 && (s.peek()=='\'' || s.peek()=='{' || s.peek()=='$')) { // Default argument
                string key = outputs[0];
                parameters += key; // May not be defined yet
                if(s.match('\'')) {
                    string value = s.until('\''); // Literal
                    assert_(!defaultArguments.contains(key),"Multiple default argument definitions for",key);
                    defaultArguments.insert(String(key), String(value));
                    if(!arguments.contains(key)) arguments.insert(String(key), String(value));
                }
                else error("Unquoted literal", key, s.whileNo(" \t\r\n"_));
            } else {
                Rule rule;
                string word = s.word("_-"_);
                assert_(word, "Expected operator or input for", outputs);
                if(!Interface<Operation>::factories.contains(word)) rule.inputs << word; // Forwarding rule
                else rule.operation = word; // Generating rule
                s.whileAny(" \t\r"_);
                for(;!s.match('\n'); s.whileAny(" \t\r"_)) {
                    if(s.match('#')) { s.whileNot('\n'); continue; }
                    string key = s.word("_-."_); s.whileAny(" \t\r"_);
                    assert_(key, s.until('\n'), word);
                    if(s.match('=')) { // Local argument
                        s.whileAny(" \t\r"_);
                        s.skip("'"_);
                        rule.arguments.insert(String(key), String(s.until('\'')));
                    }
                    else if(resultNames.contains(key)) rule.inputs << key; // Result input
                    else if(rule.operation) rule.inputs << key; // Argument value
                }
                for(string output: outputs) { assert_(!resultNames.contains(output), "Multiple result definitions for", output); resultNames << output; }
                rule.outputs = move(outputs);
                rules << move(rule);
            }
        }

        arguments.clear(); targets.clear(); results.clear(); array<string> specialArguments;
        for(const string& argument: allArguments) { // Parses generic arguments (may affect process definition)
            TextData s (argument); string key = s.word("-_."_);
            if(s.match('=')) { // Explicit argument
                string scope, parameter = key;
                if(key.contains('.')) scope=section(key, '.', 0, 1), parameter=section(key, '.', 1, 2);
                //assert_(parameters.contains(parameter),"Invalid parameter", parameter);
                string value = s.untilEnd();
                assert_(value);
                assert_(!arguments.contains(key), key);
                arguments.insert(String(key), String(value));
            }
            else if(resultNames.contains(argument)) targets << argument;
            else if(parameters.contains(argument)) arguments.insert(String(argument), String());
            else specialArguments << argument;
        }
        for(auto arg: defaultArguments) if(!arguments.contains(arg.key)) arguments.insert(copy(arg.key), copy(arg.value));
        results.grow(targets.size);
        for(uint i: range(targets.size)) {
            array<string> stack; stack << targets[i];
            while(stack) { // Traverse dependency to get all intermediate result names
                string result = stack.pop();
                results[i] += result;
                const Rule& rule = ruleForOutput(result);
                if(&rule) stack << rule.inputs;
            }
        }
        this->parseSpecialArguments(specialArguments);
    }
    return targets;
}

Rule& Process::ruleForOutput(const string& target) { for(Rule& rule: rules) for(const string& output: rule.outputs) if(output==target) return rule; return *(Rule*)0; }

const Dict& Process::relevantArguments(const string& target, const Dict& arguments) {
    for(const Evaluation& e: cache) if(e.target==target && e.input==arguments) return e.output;
    const Rule& rule = ruleForOutput(target);
    Dict args;
    if(!&rule && arguments.contains(target)) { // Conversion from argument to result
        args.insert(String(target), copy(arguments.at(target)));
        assert_(args);
        cache << unique<Evaluation>(String(target), copy(arguments), move(args));
        return cache.last()->output;
    }
    assert_(&rule, "No rule generating '"_+target+"'"_, arguments);

    // Recursively evaluates relevant arguments to invalid cache
    for(const string& input: rule.inputs) {
        for(auto arg: relevantArguments(input, arguments)) {
            assert_(args.value(arg.key,arg.value)==arg.value, target, arg.key, args.at(arg.key), arg.value);
            if(!args.contains(arg.key)) args.insert(copy(arg.key), copy(arg.value));
        }
    }
    array<string> parameters = rule.parameters();
    for(auto arg: arguments) { // Appends relevant global arguments
        assert_(args.value(arg.key,arg.value)==arg.value);
        if(parameters.contains(arg.key)) if(!args.contains(arg.key)) args.insert(copy(arg.key), copy(arg.value));
    }
    for(auto arg: arguments) { // Appends matching scoped arguments
        string scope, parameter = arg.key;
        if(arg.key.contains('.')) scope=section(arg.key, '.', 0, 1), parameter=section(arg.key, '.', 1, 2);
        if(!scope) continue;
        for(const string& output: rule.outputs) if(output==scope) goto match;
        /*else*/ continue;
match:
        assert_(parameters.contains(parameter), "Irrelevant parameter", scope+"."_+parameter, "for"_, rule);
        if(args.contains(parameter)) args.remove(parameter);
        args.insert(copy(arg.key), copy(arg.value));
    }
    cache << unique<Evaluation>(String(target), copy(arguments), move(args));
    return cache.last()->output;
}

/// Returns relevant global arguments and matching scoped arguments
Dict Process::localArguments(const string& target, const Dict& arguments) {
    Dict args = copy(relevantArguments(target, arguments));
    const Rule& rule = ruleForOutput(target);
    array<string> parameters = rule.parameters();
    for(auto arg: rule.arguments) { // Appends local arguments
        assert_(parameters.contains(arg.key));
        if(args.contains(arg.key)) args.remove(arg.key); // Local arguments overrides scope arguments
        args.insert(copy(arg.key), copy(arg.value));
    }
    for(auto arg: arguments) { // Appends matching scoped arguments
        string scope, parameter = arg.key;
        if(arg.key.contains('.')) scope=section(arg.key, '.', 0, 1), parameter=section(arg.key, '.', 1, 2);
        if(!scope) continue;
        for(const string& output: rule.outputs) if(output==scope) goto match;
        /*else*/ continue;
match:
        assert_(parameters.contains(parameter), "Irrelevant parameter", scope+"."_+parameter, "for"_, rule);
        if(args.contains(parameter)) args.remove(parameter);
        args.insert(String(parameter), copy(arg.value));
    }
    return args;
}

int Process::indexOf(const string& target, const Dict& arguments) {
    const Dict& localArguments = Process::localArguments(target, arguments);
    for(uint i: range(results.size)) if(results[i]->name==target && results[i]->localArguments==localArguments) return i;
    return -1;
}
const shared<Result>& Process::find(const string& target, const Dict& arguments) { int i = indexOf(target, arguments); return i>=0 ? results[i] : *(shared<Result>*)0; }

/// Returns if computing \a target with \a arguments would give the same result now compared to \a queryTime
bool Process::sameSince(const string& target, int64 queryTime, const Dict& arguments) {
    const Rule& rule = ruleForOutput(target);
    if(!&rule && arguments.contains(target)) return true; // Conversion from argument to result
    assert_(&rule, target);
    const shared<Result>& result = find(target, arguments);
    if(&result) {
        if(result->timestamp <= queryTime) queryTime = result->timestamp; // Result is still valid if inputs didn't change since it was generated
        else return false; // Result changed since query
    }
    for(const string& input: rule.inputs) { // Inputs changed since result (or query if result was discarded) was last generated
        if(!sameSince(input, queryTime, arguments)) return false;
    }
    if(rule.operation && parse(Interface<Operation>::version(rule.operation))*1000000000l > queryTime) return false; // Implementation changed since query
    return true;
}

array<string> PersistentProcess::configure(const ref<string>& allArguments, const string& definition) {
    assert_(!results);
    array<string> targets = Process::configure(allArguments, definition);
    if(arguments.contains("storageFolder"_)) storageFolder = Folder(arguments.at("storageFolder"_),currentWorkingDirectory());
    // Maps intermediate results from file system
    for(const String& path: storageFolder.list(Files|Folders)) {
        TextData s (path); string name = s.whileNot('{');
        if(path==name || !&ruleForOutput(name)) { // Removes invalid data
            if(existsFolder(path,storageFolder)) {
                for(const string& file: Folder(path,storageFolder).list(Files)) ::remove(file,Folder(path,storageFolder));
                ::removeFolder(path, storageFolder);
            } else ::remove(path, storageFolder);
            continue;
        }
        Dict arguments = parseDict(s); s.until("."_); string metadata = s.untilEnd();
        if(!existsFolder(path, storageFolder)) {
            File file = File(path, storageFolder, ReadWrite);
            if(file.size()<pageSize) { // Small file (<4K)
                results << shared<ResultFile>(name, file.modifiedTime(), move(arguments), String(metadata), file.read(file.size()), path, storageFolder);
            } else { // Memory-mapped file
                results << shared<ResultFile>(name, file.modifiedTime(), move(arguments), String(metadata), Map(file, Map::Prot(Map::Read|Map::Write)), path, storageFolder);
            }
        } else { // Folder
            Folder folder (path, storageFolder);
            shared<ResultFile> result(name, folder.modifiedTime(), move(arguments), String(metadata), String(), path, storageFolder);
            for(const String& path: folder.list(Files|Sorted)) {
                string key = section(path,'.',0,1), metadata=section(path,'.',1,-1);
                assert_(metadata == result->metadata);
                File file = File(path, folder, ReadWrite);
                if(file.size()<pageSize) { // Small file (<4K)
                    result->elements.insert(String(key), file.read(file.size()));
                } else { // Memory-mapped file
                    result->maps << Map(file, Map::Prot(Map::Read|Map::Write));
                    result->elements.insert(String(key), buffer<byte>(result->maps.last()));
                }
            }
            results << move(result);
        }
    }
    return move(targets);
}

PersistentProcess::~PersistentProcess() {
    results.clear();
    if(arguments.value("clean"_,"0"_)!="0"_) {
        for(const String& path: storageFolder.list(Files)) ::remove(path, storageFolder); // Cleanups all intermediate results
        remove(storageFolder);
    }
}

shared<Result> PersistentProcess::getResult(const string& target, const Dict& arguments) {
    const Rule& rule = ruleForOutput(target);
    if(!&rule && arguments.contains(target)) // Conversion from argument to result
        return shared<ResultFile>(target, 0, Dict(), String("argument"_), copy(arguments.at(target)), ""_, ""_);
    assert_(&rule, "Unknown rule", target, "(or failed argument or sweep conversion");

    // Simple forwarding rule
    if(!rule.operation) {
        assert_(!rule.arguments && rule.inputs.size == 1 && rule.outputs.size==1, "FIXME: Only single inputs can be forwarded");
        return getResult(rule.inputs.first(), arguments);
    }

    const shared<Result>& result = find(target, arguments);
    if(&result && sameSince(target, result->timestamp, arguments)) return share(result); // Returns a cached result if still valid
    // Otherwise regenerates target using new inputs, arguments and/or implementations

    array<shared<Result>> inputs;
    for(const string& input: rule.inputs) inputs << getResult(input, arguments);
    for(const shared<Result>& input: inputs) {
        ResultFile& result = *(ResultFile*)input.pointer;
        if(result.fileName) touchFile(result.fileName, result.folder, false); // Updates last access time for correct LRU cache behavior
    }

    unique<Operation> operation = Interface<Operation>::instance(rule.operation);
    const Dict& localArguments = this->localArguments(target, arguments);

    array<shared<Result>> outputs;
    for(uint index: range(rule.outputs.size)) {
        const string& output = rule.outputs[index];

        if(&find(output, arguments)) { // Reuses same result file
            shared<ResultFile> result = results.take(indexOf(output, arguments));
            rename(move(result->fileName), output, storageFolder);
        }

        Map map;
        int64 outputSize = operation->outputSize(localArguments, cast<Result*>(inputs), index);
        if(outputSize) { // Creates (or resizes) and maps an output result file
            while(outputSize > (existsFile(output, storageFolder) ? File(output, storageFolder).size() : 0) + freeSpace(storageFolder)) {
                long minimum=realTime(); String oldest;
                for(String& path: storageFolder.list(Files)) { // Discards oldest unused result (across all process hence the need for ResultFile's inter process reference counter)
                    TextData s (path); s.until('}'); int userCount=s.mayInteger(); if(userCount>1 || !s.match('.')) continue; // Used data or not a process data
                    if(File(path, storageFolder).size() < 64*1024) continue; // Keeps small result files
                    long timestamp = File(path, storageFolder).accessTime();
                    if(timestamp < minimum) minimum=timestamp, oldest=move(path);
                }
                if(!oldest) { if(outputSize<=1l<<32) error("Not enough space available"); else break; /*Virtual*/ }
                TextData s (oldest); string name = s.whileNot('{'); Dict localArguments = parseDict(s);
                for(uint i: range(results.size)) if(results[i]->name==name && results[i]->localArguments==localArguments) {
                    ((shared<ResultFile>)results.take(i))->fileName.clear(); // Prevents rename
                    break;
                }
                if(!existsFile(oldest, storageFolder) || outputSize > File(oldest,storageFolder).size() + freeSpace(storageFolder)) { // Removes if not a file or need to recycle more than one file
                    if(existsFile(oldest, storageFolder)) ::remove(oldest, storageFolder);
                    else { // Array output (folder)
                        Folder folder(oldest, storageFolder);
                        for(const String& path: folder.list(Files)) ::remove(path, folder);
                        remove(folder);
                    }
                    continue;
                }
                ::rename(storageFolder, oldest, storageFolder, output); // Renames last discarded file instead of removing (avoids page zeroing)
                break;
            }

            File file(output, storageFolder, Flags(ReadWrite|Create));
            file.resize(outputSize);
            if(outputSize>=pageSize) map = Map(file, Map::Prot(Map::Read|Map::Write), Map::Flags(Map::Shared|(outputSize>1l<<32?0:Map::Populate)));
        }
        outputs << shared<ResultFile>(output, currentTime(), Dict(), String(), move(map), output, storageFolder);
    }
    assert_(outputs);

    Time time;
    operation->execute(localArguments, cast<Result*>(outputs), cast<Result*>(inputs));
    if((uint64)time>0) log(rule, localArguments, time);

    for(shared<Result>& output : outputs) {
        shared<ResultFile> result = move(output);
        result->timestamp = realTime();
        result->localArguments = copy(localArguments);
        if(result->elements) { // Copies each elements data from anonymous memory to files in a folder
            assert_(!result->maps && !result->data);
            assert_(result->elements.size() > 1);
            Folder folder(result->fileName, result->folder, true);
            for(const_pair<String,buffer<byte>> element: (const map<String,buffer<byte>>&)result->elements) writeFile(element.key+"."_+result->metadata, element.value, folder);
            touchFile(result->fileName, result->folder, true);
        } else { // Synchronizes file mappings with results
            size_t mappedSize = 0;
            if(result->maps) {
                assert_(result->maps.size == 1);
                mappedSize = result->maps[0].size;
                assert_(mappedSize);
                if(mappedSize<pageSize) result->data = copy(result->data); // Copies to anonymous memory before unmapping
                result->maps.clear();
            }
            File file = 0;
            if(mappedSize && result->data.size <= mappedSize) { // Truncates file to result size
                file = File(result->fileName, result->folder, ReadWrite);
                if(result->data.size < mappedSize) file.resize(result->data.size);
                //else only open file to map read-only
            } else { // Copies data from anonymous memory to file
                file = File(result->fileName, result->folder, Flags(ReadWrite|Truncate|Create));
                assert(result->data);
                file.write(result->data);
            }
            if(result->data.size>=pageSize) { // Remaps file read-only (will be remapped Read|Write whenever used as output again)
                result->maps << Map(file);
                result->data = buffer<byte>(result->maps.last());
            }
        }
        result->rename();
        results << move(result);
    }
    return share(find(target, arguments));
}
