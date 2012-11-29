#pragma once
#include "array.h"
#include "string.h"
#include "map.h"
#include "process.h"

/// word is an index in a string table allowing fast move/copy/compare
extern array<string> pool;
struct word {
    int id;
    word(const string& s) { id=pool.indexOf(s); if(id<0) { id=pool.size(); pool<<copy(s); } }
    word(const ref<byte>& s):word(string(s)){}
    explicit operator bool() const { return pool[id].size(); }
};
inline bool operator ==(word a, word b) { return a.id == b.id; }
inline const string& str(const word& w) { return pool[w.id]; }

/// An Element generated by a Production
struct Module  {
    word symbol;
    array<float> arguments;
    Module(const word& symbol, array<float>&& arguments=array<float>()):symbol(symbol),arguments(move(arguments)){}
    operator word() const { return symbol; }
};
inline string str(const Module& o) { return str(o.symbol)+"("_+str(o.arguments,',')+")"_; }
inline Module copy(const Module& o){return Module(o.symbol,copy(o.arguments));}

struct LSystem;

/// Abstract base class to represent expressions
struct Expression {
    virtual float evaluate(LSystem&, ref<float>) const = 0;
    virtual string str() const = 0;
};
inline string str(const Expression& o) { return o.str(); }

struct Immediate : Expression {
    float value;
    Immediate(float value):value(value){}
    float evaluate(LSystem&, ref<float>) const override { return value; }
    string str() const override { return ftoa(value); }
};
struct Parameter : Expression {
    uint index;
    Parameter(uint index):index(index){}
    float evaluate(LSystem& l, ref<float> a) const override;
    string str() const override { return "$"_+dec(index); }
};
struct Operator : Expression {
    byte op;
    unique<Expression> left,right;
    Operator(byte op, unique<Expression>&& left, unique<Expression>&& right):op(op),left(move(left)),right(move(right)){}
    float evaluate(LSystem& l, ref<float> a) const override;
    string str() const override { return left->str()+::str(op)+right->str(); }
};

/// Produced by a Rule
struct Production {
    word symbol;
    array<unique<Expression> > arguments;
    virtual Module operator()(LSystem& l, ref<float> parameters) const {
        Module m(symbol);
        for(const unique<Expression>& e: arguments) m.arguments << e->evaluate(l,parameters);
        return m;
    }
    Production(const word& symbol):symbol(symbol){}
};
inline string str(const Production& o) { return o.arguments?string(str(o.symbol)+"("_+str(o.arguments,',')+")"_):copy(str(o.symbol)); }

/// Context-sensitive L-System rule
struct Rule {
    word edge;
    array<word> left; array<word> right;
    unique<Expression> condition = unique<Immediate>(true);
    array<Production> productions;
    Rule(const word& edge):edge(edge){}
};
inline string str(const Rule& o) { return /*str(o.left)+"<"_+*/str(o.edge)/*+">"_+str(o.right)*/+" → "_+str(o.productions); }

/// Bracketed, Stochastic, context-sensitive, parametric L-System definition parser and generator
struct LSystem {
    string name;
    array<Rule> rules;
    array<Module> axiom;
    map<string,float> constants;
    //ref<byte> ignore;

    unique<Expression> parse(const array<ref<byte> >& parameters, unique<Expression>&& e, struct TextData& s);

    LSystem(){}
    LSystem(string&& name, const ref<byte>& source);
    array<Module> generate(int level);

    uint currentLine=0; // Current line being parsed
    bool parseErrors=false; // Whether any errors were reported
    template<class... Args> void parseError(const Args&... args) { parseErrors=true; userError(string(str(currentLine)+":"_),args ___); }
};
