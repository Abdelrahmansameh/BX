#pragma once

#include <memory>
#include <ostream>
#include <string>

#include "antlr4-runtime.h"

#ifndef DECLARE_HEAP_STRUCT
#define DECLARE_HEAP_STRUCT(Cls)                                               \
  struct Cls;                                                                  \
  using Cls##Ptr = std::unique_ptr<Cls const>;
#define CONSTRUCTOR(Cls, ...)                                                  \
  template <typename... Args> static Cls##Ptr make(Args &&... args) {          \
    return std::unique_ptr<Cls>{new Cls(std::forward<Args>(args)...)};         \
  }                                                                            \
                                                                               \
private:                                                                       \
  explicit Cls(__VA_ARGS__)
#endif

#ifndef FORBID_COPY
#define FORBID_COPY(Cls) Cls(Cls const &) = delete;
#endif

namespace bx {

////////////////////////////////////////////////////////////////////////////////
// Source AST

namespace source {
////////////////////////////////////////////////////////////////////////////////
// Types
//enum class Type : int8_t { INT64 = 0, BOOL = 1, UNKNOWN = -1 };

struct Type{
  virtual std::ostream &print(std::ostream &out) const = 0;
  virtual ~Type() = default;
};
std::ostream &operator<<(std::ostream &out, Type const &e);

struct INT64: Type{
  INT64(){};
  std::ostream &print(std::ostream &out) const override;
};

struct BOOL: Type{
  BOOL(){};
  std::ostream &print(std::ostream &out) const override;
};

struct UNKNOWN : Type{
  UNKNOWN(){};
  std::ostream &print(std::ostream &out) const override;
};

struct POINTER: Type{
  Type* typ;
  POINTER(Type* typ): typ(typ) {} 
  std::ostream &print(std::ostream &out) const override;
};

struct LIST: Type{
  Type* typ;
  int length;
  LIST(Type* typ, int length): typ(typ), length(length) {}
  std::ostream &print(std::ostream &out) const override;
};

int sizeOf(Type* typ);
//std::ostream &operator<<(std::ostream &out, Type const ty);


// clang-format off
enum class Binop : int8_t {
  Add, Subtract, Multiply, Divide, Modulus,
  BitAnd, BitOr, BitXor, Lshift, Rshift,
  Lt, Leq, Gt, Geq, Eq, Neq,
  BoolAnd, BoolOr
};
// clang-format on
std::ostream &operator<<(std::ostream &, const Binop);

enum class Unop : int8_t { Negate, BitNot, LogNot };
std::ostream &operator<<(std::ostream &out, const Unop);

////////////////////////////////////////////////////////////////////////////////
// AST Nodes

struct ASTNode {
  virtual std::ostream &print(std::ostream &out) const = 0;
  virtual ~ASTNode() = default;
};
std::ostream &operator<<(std::ostream &out, ASTNode const &e);
#define MAKE_PRINTABLE std::ostream &print(std::ostream &out) const override;

////////////////////////////////////////////////////////////////////////////////
// Expressions

DECLARE_HEAP_STRUCT(Expr)
DECLARE_HEAP_STRUCT(IntConstant)
DECLARE_HEAP_STRUCT(BoolConstant)
DECLARE_HEAP_STRUCT(UnopApp)
DECLARE_HEAP_STRUCT(BinopApp)
DECLARE_HEAP_STRUCT(Call)
DECLARE_HEAP_STRUCT(Alloc)
DECLARE_HEAP_STRUCT(Null)
DECLARE_HEAP_STRUCT(Address)
//Assignables
DECLARE_HEAP_STRUCT(Variable)
DECLARE_HEAP_STRUCT(ListElem)
DECLARE_HEAP_STRUCT(Deref)

struct ExprVisitor {
#define VISITOR(Cls)                                                           \
  virtual void visit(Cls const &) = 0;                                         \
  void visit(Cls##Ptr &c) { visit(*c); }
  VISITOR(Variable)
  VISITOR(IntConstant)
  VISITOR(BoolConstant)
  VISITOR(UnopApp)
  VISITOR(BinopApp)
  VISITOR(Call)
  VISITOR(Alloc)
  VISITOR(Null)
  VISITOR(Address)
  VISITOR(ListElem)
  VISITOR(Deref)
#undef VISITOR
};

struct Addressor{
#define GET_ADDRESS(Cls)                                                           \
  virtual void visitAddress(Cls const &) = 0;                                         \
  void visitAddress(Cls##Ptr &c) { visitAddress(*c); }
  GET_ADDRESS(Variable)
  GET_ADDRESS(ListElem)
  GET_ADDRESS(Deref)
#undef GET_ADDRESS
};

struct Expr : public ASTNode {
  struct Meta {
    Type* ty;
    bool assignable;
  };
  std::unique_ptr<Meta> meta{new Meta{new UNKNOWN(), false}};
  virtual int binding_priority() const { return INT_MAX; }
  virtual void accept(ExprVisitor &vis) const = 0;
  virtual void acceptAddress(Addressor &addressor) const = 0;
  virtual int* getArg() const { return NULL;}
};

#define MAKE_VISITABLE                                                         \
  void accept(ExprVisitor &vis) const final { vis.visit(*this); }

#define MAKE_ADDRESSABLE                                                         \
  void acceptAddress(Addressor &adressor) const final { adressor.visitAddress(*this); }

#define NOT_ADDRESSABLE                                                        \
  void acceptAddress(Addressor &adressor) const final {(void)&adressor; return; }

struct Variable : public Expr {
  std::string label;
  MAKE_PRINTABLE
  MAKE_VISITABLE
  MAKE_ADDRESSABLE
  CONSTRUCTOR(Variable, std::string label) : label{label} {}
};

struct IntConstant : public Expr {
  const int64_t value;
  MAKE_PRINTABLE
  MAKE_VISITABLE
  NOT_ADDRESSABLE
  CONSTRUCTOR(IntConstant, int64_t value) : value(value) {}
  int* getArg() const override { return new int(value);}
};

struct BoolConstant : public Expr {
  const bool value;
  MAKE_PRINTABLE
  MAKE_VISITABLE
  NOT_ADDRESSABLE
  CONSTRUCTOR(BoolConstant, bool value) : value(value) {}
  int* getArg() const override { return new int(value ? 1 : 0);}
};

struct UnopApp : public Expr {
  const Unop op;
  ExprPtr arg;
  int binding_priority() const override;
  MAKE_PRINTABLE
  MAKE_VISITABLE
  NOT_ADDRESSABLE
  FORBID_COPY(UnopApp)
  CONSTRUCTOR(UnopApp, Unop op, ExprPtr arg) : op(op), arg{std::move(arg)} {}
};

struct BinopApp : public Expr {
  const Binop op;
  ExprPtr left_arg, right_arg;
  int binding_priority() const override;
  MAKE_PRINTABLE
  MAKE_VISITABLE
  NOT_ADDRESSABLE
  FORBID_COPY(BinopApp)
  CONSTRUCTOR(BinopApp, ExprPtr left_arg, Binop op, ExprPtr right_arg)
      : op(op), left_arg{std::move(left_arg)}, right_arg{std::move(right_arg)} {
  }
};

struct Call : public Expr {
  std::string func;
  std::vector<ExprPtr> args;
  MAKE_PRINTABLE
  MAKE_VISITABLE
  NOT_ADDRESSABLE
  FORBID_COPY(Call)
  CONSTRUCTOR(Call, std::string const &func, std::vector<ExprPtr> &args)
      : func(func), args(std::move(args)) {}
};

struct Alloc : public Expr {
  ExprPtr size;
  Type* typ; 
  MAKE_PRINTABLE
  MAKE_VISITABLE
  NOT_ADDRESSABLE
  FORBID_COPY(Alloc)
  CONSTRUCTOR(Alloc, ExprPtr size, Type* typ ): size(std::move(size)), typ(typ) {}
};

struct Null : public Expr{
  MAKE_PRINTABLE
  MAKE_VISITABLE
  NOT_ADDRESSABLE
  FORBID_COPY(Null)
  CONSTRUCTOR(Null) {};
}; 

struct Address : public Expr{
  ExprPtr src;
  MAKE_PRINTABLE
  MAKE_VISITABLE
  NOT_ADDRESSABLE
  FORBID_COPY(Address)
  CONSTRUCTOR(Address, ExprPtr src): src(std::move(src)) {}
};

struct ListElem : public Expr{
  ExprPtr lst;
  ExprPtr idx;
  MAKE_PRINTABLE
  MAKE_VISITABLE
  MAKE_ADDRESSABLE
  FORBID_COPY(ListElem)
  CONSTRUCTOR(ListElem, ExprPtr lst, ExprPtr idx): lst(std::move(lst)),
             idx(std::move(idx)) {}
};

struct Deref : public Expr{
  ExprPtr ptr;
  MAKE_PRINTABLE
  MAKE_VISITABLE
  MAKE_ADDRESSABLE
  FORBID_COPY(Deref)
  CONSTRUCTOR(Deref, ExprPtr ptr) : ptr(std::move(ptr)) {} 
};

#undef MAKE_VISITABLE
#undef MAKE_ADDRESSABLE

////////////////////////////////////////////////////////////////////////////////
// Statements

DECLARE_HEAP_STRUCT(Stmt)
DECLARE_HEAP_STRUCT(Assign)
DECLARE_HEAP_STRUCT(Eval)
DECLARE_HEAP_STRUCT(Print)
DECLARE_HEAP_STRUCT(Block)
DECLARE_HEAP_STRUCT(IfElse)
DECLARE_HEAP_STRUCT(While)
DECLARE_HEAP_STRUCT(Declare)
DECLARE_HEAP_STRUCT(Return)

struct StmtVisitor {
#define VISITOR(Cls)                                                           \
  virtual void visit(Cls const &) = 0;                                         \
  void visit(Cls##Ptr &c) { visit(*c); }
  VISITOR(Assign)
  VISITOR(Eval)
  VISITOR(Print)
  VISITOR(Block)
  VISITOR(IfElse)
  VISITOR(While)
  VISITOR(Declare)
  VISITOR(Return)
#undef VISITOR
};

struct Stmt : public ASTNode {
  virtual void accept(StmtVisitor &vis) const = 0;
};

#define MAKE_VISITABLE                                                         \
  void accept(StmtVisitor &vis) const final { vis.visit(*this); }

struct Print : public Stmt {
  ExprPtr arg;
  MAKE_PRINTABLE
  MAKE_VISITABLE
  FORBID_COPY(Print)
  CONSTRUCTOR(Print, ExprPtr arg) : arg{std::move(arg)} {}
};

struct Assign : public Stmt {
  ExprPtr left;
  ExprPtr right;
  MAKE_PRINTABLE
  MAKE_VISITABLE
  FORBID_COPY(Assign)
  CONSTRUCTOR(Assign, ExprPtr left, ExprPtr right)
      : left{std::move(left)}, right{std::move(right)} {}
};

struct Eval : public Stmt {
  ExprPtr expr;
  MAKE_PRINTABLE
  MAKE_VISITABLE
  FORBID_COPY(Eval)
  CONSTRUCTOR(Eval, ExprPtr expr) : expr{std::move(expr)} {}
};

struct Block : public Stmt {
  std::vector<StmtPtr> body;
  Block() : body{} {}
  MAKE_PRINTABLE
  MAKE_VISITABLE
  FORBID_COPY(Block)
  CONSTRUCTOR(Block, std::vector<StmtPtr> &body) : body{std::move(body)} {}
};

struct IfElse : public Stmt {
  ExprPtr condition;
  StmtPtr true_branch, false_branch;
  MAKE_PRINTABLE
  MAKE_VISITABLE
  FORBID_COPY(IfElse)
  CONSTRUCTOR(IfElse, ExprPtr condition, StmtPtr true_branch,
              StmtPtr false_branch)
      : condition{std::move(condition)}, true_branch{std::move(true_branch)},
        false_branch{std::move(false_branch)} {}
};

struct While : public Stmt {
  ExprPtr condition;
  StmtPtr loop_body;
  MAKE_PRINTABLE
  MAKE_VISITABLE
  FORBID_COPY(While)
  CONSTRUCTOR(While, ExprPtr condition, StmtPtr loop_body)
      : condition{std::move(condition)}, loop_body{std::move(loop_body)} {}
};

struct Declare : public Stmt {
  std::string var;
  Type* ty;
  ExprPtr init;
  MAKE_PRINTABLE
  MAKE_VISITABLE
  FORBID_COPY(Declare)
  CONSTRUCTOR(Declare, std::string const &var, Type* ty, ExprPtr init)
      : var(var), ty(ty), init{std::move(init)} {}
};

struct Return : public Stmt {
  ExprPtr arg;
  MAKE_PRINTABLE
  MAKE_VISITABLE
  FORBID_COPY(Return)
  CONSTRUCTOR(Return, ExprPtr arg) : arg{std::move(arg)} {}
};
#undef MAKE_VISITABLE

////////////////////////////////////////////////////////////////////////////////
// Callables

DECLARE_HEAP_STRUCT(Callable)
DECLARE_HEAP_STRUCT(GlobalVar)

struct Callable : public ASTNode {
  using Params = std::vector<std::pair<std::string, Type*>>;
  std::string name;
  Params args;
  BlockPtr body;
  Type* return_ty; // return_ty == Type::UNKNOWN for procedures
  MAKE_PRINTABLE
  FORBID_COPY(Callable)
  CONSTRUCTOR(Callable, std::string const &name, Params const &args,
              BlockPtr body, Type* return_ty)
      : name{name}, args{args}, body{std::move(body)}, return_ty{return_ty} {}
};

struct GlobalVar : public ASTNode {
  std::string name;
  Type* ty;
  ExprPtr init;
  MAKE_PRINTABLE
  FORBID_COPY(GlobalVar)
  CONSTRUCTOR(GlobalVar, std::string const &name, Type* ty, ExprPtr init)
      : name{name}, ty{ty}, init{std::move(init)} {}
};
#undef MAKE_PRINTABLE

////////////////////////////////////////////////////////////////////////////////
// Variable declarations and programs

struct Program {
  using GlobalVarTable = std::unordered_map<std::string, GlobalVarPtr>;
  GlobalVarTable global_vars;
  using CallTable = std::unordered_map<std::string, CallablePtr>;
  CallTable callables;
  explicit Program(GlobalVarTable &&global_vars, CallTable &&callables)
      : global_vars{std::move(global_vars)}, callables{std::move(callables)} {}
};
std::ostream &operator<<(std::ostream &out, Program const &prog);

////////////////////////////////////////////////////////////////////////////////
// Parsing

source::Program read_program(std::string file);

} // namespace source
} // namespace bx

#undef FORBID_COPY
#undef CONSTRUCTOR
#undef DECLARE_HEAP_STRUCT