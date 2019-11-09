#include "type_check.h"

#include <algorithm>
#include <map>

namespace bx {
using namespace source;

inline void [[noreturn]] panic(std::string const &msg) {
  throw std::runtime_error(msg);
}

inline char const *ty_to_string(Type ty) {
  if (ty == Type::INT64)
    return "int64";
  if (ty == Type::BOOL)
    return "bool";
  return "<unknown>";
}

namespace check {

struct VarInfo {
  source::Type ty;
  bool is_init;
  VarInfo(Type ty, bool is_init) : ty{ty}, is_init{is_init} {}
  VarInfo(VarInfo const &) = default;
  VarInfo(VarInfo &&) = default;
};

class TypeChecker : public StmtVisitor, public ExprVisitor {
private:
  source::Program &source_prog;
  using VMap = std::map<std::string, std::unique_ptr<VarInfo>>;
  std::vector<VMap> symbol_map;
  int current_depth;
  Type current_return_ty = Type::UNKNOWN;

  VarInfo *lookup_var(std::string const &var) {
    for (int depth = current_depth; depth >= 0; depth--) {
      auto const &map = symbol_map[depth];
      auto local_search = map.find(var);
      if (local_search != map.end())
        return local_search->second.get();
    }
    return nullptr;
  }

public:
  TypeChecker(source::Program &source_prog)
      : source_prog{source_prog}, current_depth{1} {
    VMap gv_map;
    for (auto const &gv : source_prog.global_vars)
      gv_map.insert_or_assign(gv.first,
                              std::make_unique<VarInfo>(gv.second->ty, true));
    symbol_map.push_back(std::move(gv_map));
  }

  // Callables

  void visit(Callable const &cbl) {
    VMap map;
    for (auto const &param : cbl.args)
      map.insert_or_assign(param.first,
                           std::make_unique<VarInfo>(param.second, true));
    symbol_map.push_back(std::move(map));
    current_return_ty = cbl.return_ty;
    current_depth = 1;
    for (auto const &stmt : cbl.body->body)
      stmt->accept(*this);
    current_depth = 0;
    current_return_ty = Type::UNKNOWN;
    symbol_map.pop_back();
    if (cbl.return_ty != Type::UNKNOWN && !(ReturnCheck{})(cbl.body.get()))
      panic("Function " + cbl.name + " does not return in every code path");
  }

  struct ReturnCheck {
    bool operator()(StmtPtr const &stmt) { return (*this)(stmt.get()); }
    bool operator()(Stmt const *stmt) {
      if (dynamic_cast<Return const *>(stmt))
        return true;
      if (auto *if_stmt = dynamic_cast<IfElse const *>(stmt))
        return (*this)(if_stmt->true_branch) && (*this)(if_stmt->false_branch);
      if (auto *bl_stmt = dynamic_cast<Block const *>(stmt))
        return std::any_of(bl_stmt->body.crbegin(), bl_stmt->body.crend(),
                           *this);
      return false;
    }
  };

  // Statements

  void visit(Assign const &mv) override {
    auto *v_info = lookup_var(mv.left);
    if (!v_info)
      panic(std::string{"Unknown variable "} + mv.left);
    mv.right->accept(*this);
    if (v_info->ty != mv.right->meta->ty)
      panic(std::string{"lhs of type "} + ty_to_string(v_info->ty) +
            " assigned to rhs of type " + ty_to_string(mv.right->meta->ty));
    v_info->is_init = true;
  }

  void visit(Declare const &dec) override {
    auto &map = symbol_map[current_depth];
    if (map.find(dec.var) != map.end())
      panic("Variable " + dec.var + " already declared in this scope");
    visit_checked(dec.init, dec.ty);
    map.insert_or_assign(dec.var,
                         std::make_unique<VarInfo>(dec.ty, !!dec.init));
  }

  void visit(Eval const &e) override { e.expr->accept(*this); }

  void visit(Print const &pr) override { pr.arg->accept(*this); }

  void visit(Block const &bl) override {
    symbol_map.push_back(VMap{});
    current_depth++;
    for (auto &stmt : bl.body)
      stmt->accept(*this);
    current_depth--;
    symbol_map.pop_back();
  }

  void visit(IfElse const &ie) override {
    ie.condition->accept(*this);
    if (ie.condition->meta->ty != Type::BOOL)
      panic("if condition is not a bool expression");
    ie.true_branch->accept(*this);
    ie.false_branch->accept(*this);
  }

  void visit(While const &wl) override {
    wl.condition->accept(*this);
    if (wl.condition->meta->ty != Type::BOOL)
      panic("while condition is not a bool expression");
    wl.loop_body->accept(*this);
  }

  void visit(Return const &ret) override {
    if (ret.arg)
      visit_checked(ret.arg, current_return_ty);
  }

  // Expressions

  // invariant: after visiting an expression the ty field is never UNKNOWN

  void visit(Variable const &v) override {
    auto *v_info = lookup_var(v.label);
    if (!v_info)
      panic("Variable " + v.label + " unknown");
    if (!v_info->is_init)
      panic("Read from uninitialized variable " + v.label + " at depth " +
            std::to_string(current_depth));
    v.meta->ty = v_info->ty;
  }

  void visit(IntConstant const &i) override { i.meta->ty = Type::INT64; }

  void visit(BoolConstant const &b) override { b.meta->ty = Type::BOOL; }

  void visit_checked(ExprPtr const &e, Type expected) {
    e->accept(*this);
    if (e->meta->ty != expected) {
      std::ostringstream ss;
      ss << "type mismatch on: \"" << *e << "\": expected " << expected
         << ", got " << e->meta->ty;
      panic(ss.str());
    }
  }

  void visit(BinopApp const &bo) override {
    switch (bo.op) {
    case Binop::Add:
    case Binop::Subtract:
    case Binop::Multiply:
    case Binop::Divide:
    case Binop::Modulus:
    case Binop::BitAnd:
    case Binop::BitOr:
    case Binop::BitXor:
    case Binop::Lshift:
    case Binop::Rshift:
      visit_checked(bo.left_arg, Type::INT64);
      visit_checked(bo.right_arg, Type::INT64);
      bo.meta->ty = Type::INT64;
      break;
    case Binop::Lt:
    case Binop::Leq:
    case Binop::Gt:
    case Binop::Geq:
      visit_checked(bo.left_arg, Type::INT64);
      visit_checked(bo.right_arg, Type::INT64);
      bo.meta->ty = Type::BOOL;
      break;
    case Binop::BoolAnd:
    case Binop::BoolOr:
      visit_checked(bo.left_arg, Type::BOOL);
      visit_checked(bo.right_arg, Type::BOOL);
      bo.meta->ty = Type::BOOL;
      break;
    case Binop::Eq:
    case Binop::Neq:
      bo.left_arg->accept(*this);
      visit_checked(bo.right_arg, bo.left_arg->meta->ty);
      bo.meta->ty = Type::BOOL;
      break;
    }
  }

  void visit(UnopApp const &uo) override {
    switch (uo.op) {
    case Unop::Negate:
    case Unop::BitNot:
      visit_checked(uo.arg, Type::INT64);
      uo.meta->ty = Type::INT64;
      break;
    case Unop::LogNot:
      visit_checked(uo.arg, Type::BOOL);
      uo.meta->ty = Type::BOOL;
      break;
    }
  }

  void visit(Call const &ca) override {
    auto const &cbl = source_prog.callables.find(ca.func);
    if (cbl == source_prog.callables.end())
      panic("Unknown function/procedure: " + ca.func);
    auto const &params = cbl->second->args;
    if (ca.args.size() != params.size())
      panic("Expected " + std::to_string(params.size()) + " arguments, got " +
            std::to_string(ca.args.size()));
    for (size_t i = 0; i < ca.args.size(); i++)
      visit_checked(ca.args[i], params[i].second);
    ca.meta->ty = cbl->second->return_ty;
  }
};

void type_check(Program &src_prog) {
  TypeChecker tyc{src_prog};
  for (auto &cbl : src_prog.callables)
    tyc.visit(*cbl.second);
  // check that the main() proc is present
  auto const &main_proc = src_prog.callables.find("main");
  if (main_proc == src_prog.callables.end() ||
      main_proc->second->return_ty != Type::UNKNOWN)
    panic("Cannot find main() procedure");
}

} // namespace check
} // namespace bx