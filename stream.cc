#include "stream.h"
#include "string.h"

/// TextStream

bool TextStream::matchAny(const array<char>& any) {
    if(available(1)) { char c=get(1)[0]; for(const char& e: any) if(c == e) { advance(1); return true; } }
    return false;
}

bool TextStream::match(const string& key) {
    if(available(key.size())>=key.size() && get(key.size()) == key) { advance(key.size()); return true; }
    else return false;
}

void TextStream::whileAny(const array<char>& any) { while(matchAny(any)){} }

string TextStream::until(const string& key) {
    string a;
    for(;available(key.size())>=key.size() && get(key.size()) != key;) a << read(1);
    match(key);
    return a;
}

string TextStream::untilAny(const array<char>& any) {
    string a;
    while(available(1) && !matchAny(any)) a << read(1);
    return a;
}

string TextStream::untilEnd() { uint size=available(-1); return read(size); }

void TextStream::skip() { whileAny(" \t\n\r"_); }

string TextStream::word() {
    string word;
    for(;available(1);) { char c=get(1)[0]; if(!(c>='a'&&c<='z')) break; word<<c; advance(1); }
    return word;
}

string TextStream::xmlIdentifier() {
    string identifier;
    for(;available(1);) {
        char c=get(1)[0];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==':'||c=='-'||c=='_')) break;
        identifier<<c;
        advance(1);
    }
    return identifier;
}

int TextStream::number() {
    string number;
    for(;available(1);) { char c=get(1)[0]; if(!(c>='0'&&c<='9')) break; number<<c; advance(1); }
    if(!number) return -1;
    return toInteger(number);
}
