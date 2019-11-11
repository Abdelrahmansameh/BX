#pragma once

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "ast.h"

/** This defines the RTL intermediate language */

#ifndef CONSTRUCTOR
#define CONSTRUCTOR(Cls, ...)                                                  \
  template <typename... Args>                                                  \
  static std::unique_ptr<Cls const> make(Args &&... args) {                    \
    return std::unique_ptr<Cls>{new Cls(std::forward<Args>(args)...)};         \
  }                                                                            \
                                                                               \
private:                                                                       \
  explicit Cls(__VA_ARGS__)
#endif

namespace bx {

namespace rtl {

struct Label {
  int id;
  bool operator<(Label const &other) const noexcept { return id < other.id; }
  bool operator==(Label const &other) const noexcept { return id == other.id; }
};
std::ostream &operator<<(std::ostream &out, Label const &l);

struct Pseudo {
  int id;
  bool operator==(Pseudo const &other) const noexcept { return id == other.id; }
  bool operator!=(Pseudo const &other) const noexcept { return id != other.id; }
};
std::ostream &operator<<(std::ostream &out, Pseudo const &r);
constexpr Pseudo discard_pr{-1};

struct Instr;
using InstrPtr = std::unique_ptr<Instr const>;

struct Move;
struct Copy; // copy between pseudo
struct CopyMP; // copy machine registers to pseudo
struct CopyPM; // copy pseudo to machine registers
struct Load;
struct Store;
struct Binop;
struct Unop;
struct Bbranch;
struct Ubranch;
struct Goto;
struct Call;
struct Return;

struct NewFrame;    ///////////////////////////////
struct DelFrame;    ///////////////////////////////
struct LoadParam;   ///////////////////////////////
struct Push;        ///////////////////////////////
struct Pop;         ///////////////////////////////

struct InstrVisitor {
  virtual ~InstrVisitor() = default;
#define VISIT_FUNCTION(caseclass) virtual void visit(caseclass const &) = 0
  VISIT_FUNCTION(Move);
  VISIT_FUNCTION(Copy); 
  VISIT_FUNCTION(CopyMP); 
  VISIT_FUNCTION(CopyPM); 
  VISIT_FUNCTION(Load);
  VISIT_FUNCTION(Store);
  VISIT_FUNCTION(Binop);
  VISIT_FUNCTION(Unop);
  VISIT_FUNCTION(Bbranch);
  VISIT_FUNCTION(Ubranch);
  VISIT_FUNCTION(Call);
  VISIT_FUNCTION(Return);
  VISIT_FUNCTION(Goto);
  VISIT_FUNCTION(NewFrame);  ///////////////////////////////
  VISIT_FUNCTION(DelFrame);  ///////////////////////////////
  VISIT_FUNCTION(LoadParam); ///////////////////////////////
  VISIT_FUNCTION(Push);      ///////////////////////////////
  VISIT_FUNCTION(Pop);       ///////////////////////////////

#undef VISIT_FUNCTION
};

struct Instr {
  virtual ~Instr() = default;
  virtual std::ostream &print(std::ostream &out) const = 0;
  virtual void accept(InstrVisitor &vis) = 0;
};

inline std::ostream &operator<<(std::ostream &out, Instr const &i) {
  return i.print(out);
}

#define MAKE_VISITABLE                                                         \
  void accept(InstrVisitor &vis) override { vis.visit(*this); }

struct Move : public Instr {
  int64_t source;
  Pseudo dest;
  Label succ;

  std::ostream &print(std::ostream &out) const override {
    return out << "move " << source << ", " << dest << "  --> " << succ;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(Move, int64_t source, Pseudo dest, Label succ)
      : source{source}, dest{dest}, succ{succ} {}
};

// Different Copies
struct Copy : public Instr {
  Pseudo src, dest;
  Label succ;

  std::ostream &print(std::ostream &out) const override {
    return out << "copy " << src << ", " << dest << "  --> " << succ;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(Copy, Pseudo src, Pseudo dest, Label succ)
      : src{src}, dest{dest}, succ{succ} {}
};

struct CopyMP : public Instr {
  /*enum Register : uint16_t {
    RBX, R12, R13, R14, R15
  };*/


  char const * src;
  Pseudo dest;
  Label succ;

  std::ostream &print(std::ostream &out) const override {
    return out << "copy " << src << ", " << dest << "  --> " << succ;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(CopyMP, char const * src, Pseudo dest, Label succ)
      : src{src}, dest{dest}, succ{succ} {}
};

struct CopyPM : public Instr {
  /*enum Register : uint16_t {
    // clang-format off
    RAX, RBX, R12, R13, R14, R15
    // clang-format on
  }; */

  Pseudo src;
  char const * dest;
  Label succ;

  std::ostream &print(std::ostream &out) const override {
    return out << "copy " << src << ", " << dest << "  --> " << succ;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(CopyPM, Pseudo src, char const * dest, Label succ)
      : src{src}, dest{dest}, succ{succ} {}
};


struct Load : public Instr {
  std::string src;
  int offset;
  Pseudo dest;
  Label succ;

  std::ostream &print(std::ostream &out) const override {
    return out << "load " << src << '+' << offset << ", " << dest << "  --> "
               << succ;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(Load, std::string const &src, int offset, Pseudo dest, Label succ)
      : src{src}, offset{offset}, dest{dest}, succ{succ} {}
};

struct Store : public Instr {
  Pseudo src;
  std::string dest;
  int offset;
  Label succ;

  std::ostream &print(std::ostream &out) const override {
    return out << "store " << src << ", " << dest << '+' << offset << "  --> "
               << succ;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(Store, Pseudo src, std::string const &dest, int offset,
              Label succ)
      : src{src}, dest{dest}, offset{offset}, succ{succ} {}
};

struct Unop : public Instr {
  enum Code : uint16_t { NEG, NOT };

  Code opcode;
  Pseudo arg;
  Label succ;

  std::ostream &print(std::ostream &out) const override {
    return out << "unop " << code_map.at(opcode) << ", " << arg << "  --> "
               << succ;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(Unop, Code opcode, Pseudo arg, Label succ)
      : opcode{opcode}, arg{arg}, succ{succ} {}

private:
  static const std::map<Code, char const *> code_map;
};

struct Binop : public Instr {
  enum Code : uint16_t {
    // clang-format off
    ADD, SUB, MUL, DIV, REM, SAL, SAR, AND, OR, XOR
    // clang-format on
  };

  Code opcode;
  Pseudo src, dest;
  Label succ;

  std::ostream &print(std::ostream &out) const override {
    return out << "binop " << code_map.at(opcode) << ", " << src << ", " << dest
               << "  --> " << succ;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(Binop, Code opcode, Pseudo src, Pseudo dest, Label succ)
      : opcode{opcode}, src{src}, dest{dest}, succ{succ} {}

private:
  static const std::map<Code, char const *> code_map;
};

struct Ubranch : public Instr {
  enum Code : uint16_t { JZ, JNZ };

  Code opcode;
  Pseudo arg;
  Label succ, fail;

  std::ostream &print(std::ostream &out) const override {
    return out << "ubranch " << code_map.at(opcode) << ", " << arg << "  --> "
               << succ << ", " << fail;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(Ubranch, Code opcode, Pseudo arg, Label succ, Label fail)
      : opcode{opcode}, arg{arg}, succ{succ}, fail{fail} {}

private:
  static const std::map<Code, char const *> code_map;
};

struct Bbranch : public Instr {
  enum Code : uint16_t {
    // clang-format off
    JE,  JL,  JLE,  JG,  JGE,
    JNE, JNL, JNLE, JNG, JNGE
    // clang-format on
  };

  Code opcode;
  Pseudo arg1, arg2;
  Label succ, fail;

  std::ostream &print(std::ostream &out) const override {
    return out << "bbranch " << code_map.at(opcode) << ", " << arg1 << ", "
               << arg2 << "  --> " << succ << ", " << fail;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(Bbranch, Code opcode, Pseudo arg1, Pseudo arg2, Label succ,
              Label fail)
      : opcode{opcode}, arg1{arg1}, arg2{arg2}, succ{succ}, fail{fail} {}

private:
  static const std::map<Code, char const *> code_map;
};

struct Goto : public Instr {
  Label succ;

  std::ostream &print(std::ostream &out) const override {
    return out << "goto  --> " << succ;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(Goto, Label succ) : succ{succ} {}
};

struct Call : public Instr {
  std::string func;
  int Nargs;
  Label succ;

  std::ostream &print(std::ostream &out) const override {
    out << "call " << func << "(" << Nargs;
    return out << ")" << "  --> " << succ;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(Call, std::string func, int Nargs,
              Label succ)
      : func{func}, Nargs(Nargs), succ{succ} {}
};

struct Return : public Instr {

  std::ostream &print(std::ostream &out) const override {
    return out << "return ";
  }
  MAKE_VISITABLE
  CONSTRUCTOR(Return) {}
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
struct NewFrame : public Instr {
  Label succ;
  int size;
  std::ostream &print(std::ostream &out) const override {
    return out << "newframe " << size << " --> " << succ;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(NewFrame, Label succ, int size) : succ{succ}, size{size} {}
};

struct DelFrame : public Instr {
  Label succ;

  std::ostream &print(std::ostream &out) const override {
    return out << "delframe  --> " << succ;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(DelFrame, Label succ) : succ{succ} {}
};

struct LoadParam : public Instr {
  int64_t source;
  Pseudo dest;
  Label succ;

  std::ostream &print(std::ostream &out) const override {
    return out << "load_param " << source << ", " << dest << "  --> " << succ;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(LoadParam, int64_t source, Pseudo dest, Label succ)
      : source{source}, dest{dest}, succ{succ} {}
};

struct Push : public Instr {
  Pseudo dest;
  Label succ;

  std::ostream &print(std::ostream &out) const override {
    return out << "push " << dest << "  --> " << succ;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(Push, Pseudo dest, Label succ) : dest{dest}, succ{succ} {}
};

struct Pop : public Instr {
  Pseudo dest;
  Label succ;

  std::ostream &print(std::ostream &out) const override {
    return out << "pop " << dest << "  --> " << succ;
  }
  MAKE_VISITABLE
  CONSTRUCTOR(Pop, Pseudo dest, Label succ) : dest{dest}, succ{succ} {}
};
/////////////////////////////////////////////////////////////////////////////////////////////////////
#undef MAKE_VISITABLE

struct LabelHash {
  std::size_t operator()(Label const &l) const noexcept {
    return std::hash<int>{}(l.id);
  }
};
struct LabelEq {
  constexpr bool operator()(Label const &l, Label const &r) const noexcept {
    return l.id == r.id;
  }
};
template <typename V>
using LabelMap = std::unordered_map<Label, V, LabelHash, LabelEq>;

struct Callable {
  std::string name;
  Label enter , leave;
  std::vector<Pseudo> input_regs;
  Pseudo output_reg;
  LabelMap<InstrPtr> body;
  std::vector<Label> schedule; // the order in which the labels are scheduled
  explicit Callable(std::string name) : name{name} {}
  void add_instr(Label lab, InstrPtr instr) {
    if (body.find(lab) != body.end()) {
      std::cerr << "Repeated in-label: " << lab.id << '\n';
      std::cerr << "Trying: " << lab << ": " << *instr << '\n';
      throw std::runtime_error("repeated in-label");
    }
    schedule.push_back(lab);
    body.insert_or_assign(lab, std::move(instr));
  }
};
std::ostream &operator<<(std::ostream &out, Callable const &cbl);


using Program = std::vector<Callable>;

} // namespace rtl

} // namespace bx

#undef CONSTRUCTOR