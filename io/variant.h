#pragma once
#include "string.h"
#include "map.h"
#include "data.h"
//#include "vector.h"

struct Variant {
 enum { Null, Boolean, Integer, Real, Data, List, Dict, Rational } type = Null;
 double number = 0;
 String data;
 array<Variant> list;
 map<String,Variant> dict;
 double denominator = 1;

 Variant(){}
 Variant(decltype(nullptr)) : type(Null) {}
 Variant(bool boolean) : type(Boolean), number(boolean) {}
 Variant(int number) : type(Integer), number(number) {}
 Variant(int64 number) : type(Integer), number(number) {}
 Variant(uint number) : type(Integer), number(number) {}
 Variant(size_t number) : type(Integer), number(number) {}
 Variant(double number) : type(number==int(number)?Integer:Real), number(number) {}
 Variant(string data) : type(Data), data(copyRef(data)) {}
 Variant(String&& data) : type(Data), data(move(data)) {}
 Variant(array<Variant>&& list) : type(List), list(move(list)) {}
 Variant(map<String,Variant>&& dict) : type(Dict), dict(move(dict)) {}
 Variant(int64 numerator, int64 denominator) : type(Rational), number(numerator), denominator(denominator) {}

 explicit operator bool() const { return type != Null; }

 int64 integer() const { /*assert_(type==Integer, (int)type, *this); return number;*/ return real(); } // FIXME
 double real() const {
  if(type==Rational) { assert_((number/denominator)*denominator==number); return number/denominator; }
  if(type==Real||type==Integer) return number;
  if(type==Data) return parseDecimal(data);
  error(int(type));
 }
 explicit operator string() const { assert(type==Data); return data; }
 int64 numerator() {  assert(type==Rational, *this); return number; }
 operator float() const { return real(); }
 operator int() const { return integer(); }
 operator uint() const { return integer(); }
};
inline bool operator ==(const Variant& a, const Variant& b) {
 if((a.type == Variant::Integer || a.type == Variant::Real) &&
    (b.type == Variant::Integer || b.type == Variant::Real))
  return a.number == b.number;
 if(a.type != b.type) return false;
 if(a.type == Variant::Data) return a.data == b.data;
 if(a.type == Variant::Null && b.type == Variant::Null) return true;
 error(int(a.type));
 //return (string)a == (string)b;
}
inline bool operator <(const Variant& a, const Variant& b) {
 if((a.type == Variant::Integer || a.type == Variant::Real) &&
    (b.type == Variant::Integer || b.type == Variant::Real))
  return a.number < b.number;
 if(a.type == Variant::Data && b.type == Variant::Data) {
  if(isDecimal(a.data) && isDecimal(b.data)) return parseDecimal(a.data) < parseDecimal(b.data);
  return a.data < b.data;
 }
 if(a.type == Variant::Null && b.type != Variant::Null) return true;
 if(a.type != Variant::Null && b.type == Variant::Null) return false;
 error(int(a.type), int(b.type));
 //return (string)a < (string)b;
}

template<> inline Variant copy(const Variant& v) {
 if(v.type == Variant::Integer || v.type == Variant::Real) return v.number;
 if(v.type == Variant::Data) return v.data;
 if(v.type == Variant::Null) return Variant();
 error(int(v.type));
}
generic String strPDF(const map<T,Variant>& dict) {
 array<char> s;
 s.append("<<"); for(auto entry: dict) s.append( '/'+entry.key+' '+str(entry.value)+' ' ); s.append(">>");
 return move(s);
}

inline String str(const Variant& o) {
 if(o.type==Variant::Boolean) return unsafeRef(str(bool(o.number)));
 if(o.type==Variant::Integer) { assert(o.number==int(o.number)); return str(int(o.number)); }
 if(o.type==Variant::Real || o.type==Variant::Rational) return str(o.real());
 if(o.type==Variant::Data) return copy(o.data);
 if(o.type==Variant::List) return str(o.list);
 if(o.type==Variant::Dict) return strPDF(o.dict);
 if(o.type==Variant::Null) return String();
 error("Invalid Variant",int(o.type));
}

typedef map<String,Variant> Dict; /// Associative array of variants

inline Dict parseDict(TextData& s) {
 Dict dict;
 bool curly = s.match('{');
 if(curly && s.match('}')) return dict;
 else if(!s) return dict;
 for(;;) {
  s.whileAny(" "_);
  string key = s.whileNo(":=|},"_);
  string value; s.whileAny(" "_);
  if(s.matchAny(":="_)) { s.whileAny(" "_); value = s.whileNo("|,} "_,'{','}'); }
  assert_(key /*&& value*/, "'"+key+"'", "'"+value+"'", "'"+s.slice(s.index)+"'", "'"+s.data+"'");
  if(!dict.contains(key)) dict.insertSorted(copyRef(key), replace(copyRef(value),'\\','/'));
  else {
   if(dict.at(key)==value) log("Duplicate entry with same value", key, value);
   else error("Duplicate entry with different value", key, dict.at(key), value);
  }
  s.whileAny(" "_);
  if(s.matchAny("|,"_)) continue;
  else if(curly && s.match('}')) break;
  else if(!curly && !s) break;
  else error("Invalid Dict '"+s.data.slice(s.index)+"'", (bool)s, curly, dict, s.data);
 }
 return dict;
}
inline Dict parseDict(string str) { TextData s (str); return parseDict(s); }
