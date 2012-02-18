#pragma once
#include "string.h"
#include <math.h>

/// unique string literals (unique strings stay allocated for the whole program and can be tested for equality using pointer comparison)
inline const string& operator "" __(const char* data, size_t size) {
    static array<string*> uniques; //TODO: user defined namespaces
    string unique(data,size);
    int i = uniques.find([&unique](string* s){return *s==unique;});
    if(i<0) { i=uniques.size; uniques<<new string(move(unique)); }
    return *uniques[i];
}

/// pointer to a pool-allocated value with move semantics
template<class T> struct pointer {
    no_copy(pointer)
    pointer(){}
    static const int max=65536*4; //Maximum expression complexity
    static T pool[max];
    static int count;
    explicit pointer(T&& value):value(new (pool+count++) T(move(value))){assert(count<max);}
    pointer(pointer&& o) : value(o.value) { o.value=0; }
    pointer& operator=(pointer&& o) { this->~pointer(); value=o.value; o.value=0; return *this; }
    ~pointer() { if(value) value->~Expression(); } //TODO: free
    T* value=0;
    const T& operator *() const { return *value; }
    T& operator *() { return *value; }
    const T* operator ->() const { return value; }
    T* operator ->() { return value; }
    explicit operator bool() const { return value; }
    bool operator !() const { return !value; }
    operator const T*() const { return value; }
    operator T*() { return value; }
};
template <class T> T pointer<T>::pool[pointer<T>::max];
template <class T> int pointer<T>::count=0;
template<class T> pointer<T> copy(const pointer<T>& p) { if(!p.value) return pointer<T>(); return pointer<T>(copy(*p.value)); }

// Expression type
enum Type { Invalid,    Operand/*:*/, Symbol, Integer,   Operator/*:*/, Add, Mul, Pow };
/// Represents abstract mathematical expression using a tree of binary operations
struct Expression {
    Type type=Invalid;
    //TODO: union
    int integer=0;
    const string* symbol=0;
    pointer<Expression> a, b;

    /// Move semantics allow to manage memory without reference counting nor garbage collection
    move_only(Expression)

    Expression() {} /// Default constructs an invalid Expression
    Expression(int integer) : type(Integer), integer(integer) {invariant();} /// Literal integer operand
    Expression(const string* symbol) : type(Symbol), symbol(symbol) {invariant();} /// Symbol operand /// \note \a symbol should outlive this \a Expression
    Expression(const string& symbol) : type(Symbol), symbol(addressof(symbol)) {invariant();} /// Symbol operand /// \note \a symbol should outlive this \a Expression

    Expression(Type type, pointer<Expression>&& a, pointer<Expression>&& b) : type(type), a(move(a)), b(move(b)) { /// Operator
        assert(type==Add||type==Mul||type==Pow); reduce();
    }
    /// Verify if expression is well formed
    void invariant() const;
    /// Returns true if the expression contains an occurence of e (also search operands recursively)
    bool contains(const Expression& e) const;
    /// Returns true if the expression contains an occurence of e (also search operands recursively)
    const Expression* find(bool (*match)(const Expression&)) const;
    /// Returns the number of subexpression composing this expression
    int count();
    /// Returns a readable formatted representation
    string str() const;
    /// Returns an approximation as a floating point (symbols are evaluated to 1)
    float toFloat();
    /// Performs pattern matching to simplify the expression (should be called after editing operands)
    void reduce();
    /// Tests if the expression evaluates to zero
    explicit operator bool() const {
        if(type==Integer) return integer;
        //FIXME: symbols?
        return true; //not zero
    }
};
template<> inline Expression copy(const Expression& e) {
    Expression r; r.type=e.type; r.integer=e.integer; r.symbol=e.symbol; r.a=copy(e.a); r.b=copy(e.b); return r;
}
template<> inline string str(const Expression& a) { return str(a.str()); }

//TODO: perfect forwarding to handle mixed references (move/move,move/copy,copy/move,copy/copy)
/// move semantics
#define new(a) pointer<Expression>(a)
//inline Expression operator-(pointer<Expression>&& a) { return -1*move(a); }

inline Expression operator+(pointer<Expression>&& a, pointer<Expression>&& b) { return Expression(Add,move(a),move(b)); }
inline Expression operator*(pointer<Expression>&& a, pointer<Expression>&& b) { return Expression(Mul,move(a),move(b)); }
inline Expression operator-(pointer<Expression>&& a, pointer<Expression>&& b) { return move(a)+new(move(b)*new(-1)); }
inline Expression operator^(pointer<Expression>&& a, pointer<Expression>&& b) { return Expression(Pow,move(a),move(b)); }
inline Expression operator/(pointer<Expression>&& a, pointer<Expression>&& b) { return move(a)*new(move(b)^new(-1)); }

inline Expression& operator+=(Expression& a, pointer<Expression>&& b) { return a=new(move(a))+move(b); }
inline Expression& operator-=(Expression& a, pointer<Expression>&& b) { return a=new(move(a))-move(b); }
inline Expression& operator*=(Expression& a, pointer<Expression>&& b) { return a=new(move(a))*move(b); }
inline Expression& operator/=(Expression& a, pointer<Expression>&& b) { return a=new(move(a))/move(b); }

#undef new
/// copy semantics
#define new(a) pointer<Expression>(copy(a))

inline Expression operator+(const Expression& a, const Expression& b) { return new(a)+new(b); }
inline Expression operator-(const Expression& a, const Expression& b) { return new(a)-new(b); }
inline Expression operator*(const Expression& a, const Expression& b) { return new(a)*new(b); }
inline Expression operator/(const Expression& a, const Expression& b) { assert(b); return new(a)/new(b); }
inline Expression operator^(const Expression& a, const Expression& b) { return new(a)^new(b); }

inline Expression& operator+=(Expression& a, const Expression& b) { return a=a+b; }
inline Expression& operator-=(Expression& a, const Expression& b) { return a=a-b; }
inline Expression& operator*=(Expression& a, const Expression& b) { return a=a*b; }
inline Expression& operator/=(Expression& a, const Expression& b) { return a=a/b; }

#undef new
#define new(a) pointer<Expression>(a)

Expression abs(const Expression& a);

/// comparison operators
bool operator==(const Expression& a, const Expression& b);
inline bool operator!=(const Expression& a, const Expression& b) { return !(a==b); }

bool operator>(const Expression& a, int integer);
inline bool operator>(const Expression& a, const Expression& b) { return (a-b)>0; }
