#include "string.h"
#include "math.h" //isnan/isinf

/// utf8_iterator
int utf8_iterator::operator* () const {
    int code = pointer[0];
    if(code&0b10000000) {
        bool test = code&0b00100000;
        code &= 0b00011111; code <<= 6; code |= pointer[1]&0b111111;
        if(test) {
            bool test = code&0b10000000000;
            code &= 0b00001111111111; code <<= 6; code |= pointer[2]&0b111111;
            if(test) {
                bool test = code&0b1000000000000000;
                if(test) fail();
                code &= 0b00000111111111111111; code <<= 6; code |= pointer[3]&0b111111;
            }
        }
    }
    return code;
}
const utf8_iterator& utf8_iterator::operator++() {
    int code = *pointer++;
    if(code&0b10000000) { pointer++;
        if(code&0b00100000) { pointer++;
            if(code&0b00010000) { pointer++;
                if(code&0b00001000) fail();
            }
        }
    }
    return *this;
}

/// string operations

bool operator <(const string& a, const string& b) {
    for(int i=0;i<min(a.size(),b.size());i++) {
        if(a[i] > b[i]) return false;
        if(a[i] < b[i]) return true;
    }
    return a.size() < b.size();
}

string strz(const string& s) { return s+"\0"_; }

string strz(const char* s) { if(!s) return "null"_; int i=0; while(s[i]) i++; return copy(string(s,i)); }

void section_(const string& s, char sep, int& start, int& end, bool includeSep) {
    int b,e;
    if(start>=0) {
        b=0;
        for(int i=0;i<start && b<s.size();b++) if(s[b]==sep) i++;
    } else {
        b=s.size();
        if(start!=-1) for(int i=0;b-->0;) { if(s[b]==sep) { i++; if(i>=-start-1) break; } }
        b++; //skip separator
    }
    if(end>=0) {
        e=0;
        for(int i=0;e<s.size();e++) if(s[e]==sep) { i++; if(i>=end) { if(includeSep) e++; break; } }
    } else {
        e=s.size();
        if(end!=-1) for(int i=0;e-->0;) { if(s[e]==sep) { i++; if(i>=-end-1) { if(includeSep) e++; break; } } }
    }
    start=b; end=e;
}
/*string sectionRef(const string& s, char sep, int start, int end, bool includeSep) {
    section_(s,sep,start,end,includeSep);
    return string(s.data+start,end-start);
}*/
string section(const string& s, char sep, int start, int end, bool includeSep) {
    section_(s,sep,start,end,includeSep);
    return copy(string(s.data()+start,end-start));
}
/*string section(string&& s, char sep, int start, int end, bool includeSep) {
    section_(s,sep,start,end,includeSep);
    return slice(move(s),start,end-start);
}*/

array<string> split(const string& str, char sep) {
    array<string> r;
    int b=0,e=0;
    for(;;) {
        while(b<str.size() && str[b]==sep) b++;
        e=b;
        while(e<str.size() && str[e]!=sep) e++;
        if(b==str.size()) break;
        r << slice(str,b,e-b);
        if(e==str.size()) break;
        b=e+1;
    }
    return r;
}

string replace(const string& s, const string& before, const string& after) {
    string r(s.size());
    for(int i=0;i<s.size();) { //->utf8_iterator
        if(i<=s.size()-before.size() && string(s.data()+i, before.size())==before) { r<<after; i+=before.size(); }
        else { r<<s[i]; i++; }
    }
    return r;
}

/// Human-readable value representation

string str(uint64 n, int base, int pad) {
    assert(base>=2 && base<=16,"Unsupported base"_,base);
    char buf[64]; int i=64;
    do {
        buf[--i] = "0123456789ABCDEF"[n%base];
        n /= base;
    } while( n!=0 );
    while(64-i<pad) buf[--i] = '0';
    return copy(string(buf+i,64-i));
}

string str(float n, int precision, int base) {
    if(isnan(n)) return "NaN"_;
    if(isinf(n)) return n>0?"∞"_:"-∞"_;
    int m=1; for(int i=0;i<precision;i++) m*=base;
    return (n>=0?""_:"-"_)+str(uint64(abs(n)),base)+"."_+str(uint64(m*abs(n))%m,base,precision);
}

long readInteger(const char*& s, int base) {
    assert(base>=2 && base<=16,"Unsupported base"_,base);
    int neg=0;
    if(*s == '-' ) s++, neg=1; else if(*s == '+') s++;
    long number=0;
    for(;*s;s++) {
        int n = string("0123456789ABCDEF",base).indexOf(*s);
        if(n < 0) break; //assert(n>=0,"Invalid integer",str);
        number *= base;
        number += n;
    }
    return neg ? -number : number;
}

long toInteger(const string& str, int base) { const char* s=&str; return readInteger(s,base); }

double readFloat(const char*& s, int base ) {
    assert(base>2 && base<16,"Unsupported base"_,base);
    int neg=0;
    if(*s == '-') s++, neg=1; else if(*s == '+') s++;
    int exponent = 1;
    int significand = 0;
    for(bool gotDot=false;*s;s++) {
        if(*s == '.') { gotDot=true; continue; }
        int n = string("0123456789ABCDEF",base).indexOf(*s);
        if(n < 0) break; //assert(n>=0,"Invalid float",str);
        significand *= base;
        significand += n;
        if(gotDot) exponent *= base;
    }
    return neg ? -float(significand)/float(exponent) : float(significand)/float(exponent);
}

double readFloat(const string& str, int base ) { const char* s=&str; return readFloat(s,base); }



/// Stream (TODO -> stream.cc)

//long TextStream::readInteger(int base) { auto b=(const char*)&data[pos], e=b; long r = ::readInteger(e,base); pos+=int(e-b); return r; }
//double TextStream::readFloat(int base) { auto b=(const char*)&data[pos], e=b; double r = ::readFloat(e,base); pos+=int(e-b); return r; }
