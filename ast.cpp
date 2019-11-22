#include "ast.h"

#include "BXLexer.h"
#include "BXParser.h"

namespace bx {

namespace source {

std::ostream &operator<<(std::ostream &out, const Binop op) {
  switch (op) {
    // clang-format off
  case Binop::Add: return out << '+';
  case Binop::Subtract: return out << '-';
  case Binop::Multiply: return out << '*';
  case Binop::Divide: return out << '/';
  case Binop::Modulus: return out << '%';
  case Binop::BitAnd: return out << '&';
  case Binop::BitOr: return out << '|';
  case Binop::BitXor: return out << '^';
  case Binop::Lshift: return out << "<<";
  case Binop::Rshift: return out << ">>";
  case Binop::Lt: return out << "<";
  case Binop::Leq: return out << "<=";
  case Binop::Gt: return out << ">";
  case Binop::Geq: return out << ">=";
  case Binop::Eq: return out << "==";
  case Binop::Neq: return out << "!=";
  case Binop::BoolAnd: return out << "&&";
  case Binop::BoolOr: return out << "||";
  default: return out << "<?>";
    // clang-format on
  }
}

int BinopApp::binding_priority() const {
  switch (op) {
  case Binop::Lshift:
  case Binop::Rshift:
    return 40;
  case Binop::Lt:
  case Binop::Leq:
  case Binop::Gt:
  case Binop::Geq:
    return 36;
  case Binop::Eq:
  case Binop::Neq:
    return 33;
  case Binop::BitAnd:
    return 30;
  case Binop::BitXor:
    return 20;
  case Binop::BitOr:
    return 10;
  case Binop::BoolAnd:
    return 6;
  case Binop::BoolOr:
    return 3;
  case Binop::Add:
  case Binop::Subtract:
    return 50;
  case Binop::Multiply:
  case Binop::Divide:
  case Binop::Modulus:
    return 60;
  default:
    return -1;
  }
}

std::ostream &operator<<(std::ostream &out, const Unop op) {
  switch (op) {
    // clang-format off
  case Unop::Negate: return out << '-';
  case Unop::BitNot: return out << "~";
  case Unop::LogNot: return out << "!";
  default: return out << "<?>";
    // clang-format on
  }
}

std::ostream &operator<<(std::ostream &out, ASTNode const &node) {
  return node.print(out);
}

std::ostream &Variable::print(std::ostream &out) const {
  out << this->label;
  return out;
}

std::ostream &IntConstant::print(std::ostream &out) const {
  out << this->value;
  return out;
}

std::ostream &BoolConstant::print(std::ostream &out) const {
  out << (this->value ? "true" : "false");
  return out;
}

int UnopApp::binding_priority() const {
  switch (op) {
  case Unop::BitNot:
  case Unop::Negate:
    return 70;
  case Unop::LogNot:
    return 80;
  default:
    return -1;
  }
}

std::ostream &print_bracketed(std::ostream &out, ExprPtr const &e,
                              bool bracket) {
  if (bracket)
    out << '(';
  out << *e;
  if (bracket)
    out << ')';
  return out;
}

std::ostream &UnopApp::print(std::ostream &out) const {
  bool bracket = this->binding_priority() > this->arg->binding_priority();
  out << this->op << ' ';
  return print_bracketed(out, this->arg, bracket);
}

std::ostream &BinopApp::print(std::ostream &out) const {
  print_bracketed(out, this->left_arg,
                  this->binding_priority() >
                      this->left_arg->binding_priority());
  out << ' ' << this->op << ' ';
  print_bracketed(out, this->right_arg,
                  this->binding_priority() >
                      this->right_arg->binding_priority());
  return out;
}

std::ostream &Call::print(std::ostream &out) const {
  out << func << '(';
  for (auto cur = args.begin(), end = args.end(); cur + 1 < end; cur++) {
    out << **cur << ',';
  }
  if (args.size() > 0)
    out << *(args[args.size() - 1]);
  return out << ')';
}

std::ostream &Alloc::print(std::ostream &out) const{
  return out << "alloc " << typ << " [" << *size << "]";
}

std::ostream &Null::print(std::ostream &out) const{
  return out << " null ";
}

std::ostream &Address::print(std::ostream &out) const{
  return out << "&" << *src;
}

std::ostream &ListElem::print(std::ostream &out) const{
  return out << *lst << "[" << *idx << "]"; 
}

std::ostream &Deref::print(std::ostream &out) const{
  return out << "*" << *ptr;
}

std::ostream &Print::print(std::ostream &out) const {
  return out << "print " << *arg << ';';
}

std::ostream &Assign::print(std::ostream &out) const {
  return out << *left << " = " << *right << ';';
}


std::ostream &Eval::print(std::ostream &out) const {
  return out << *expr << ';';
}

std::ostream &Block::print(std::ostream &out) const {
  out << "{ \n";
  for (auto const &stmt : this->body)
    out << *stmt << "\n";
  out << "}";
  return out;
}

std::ostream &IfElse::print(std::ostream &out) const {
  out << "if (" << *(this->condition) << ") ";
  out << *(this->true_branch) << " else " << *(this->false_branch);
  return out;
}

std::ostream &While::print(std::ostream &out) const {
  out << "while (" << *(this->condition) << ") ";
  out << *(this->loop_body);
  return out;
}

std::ostream &Declare::print(std::ostream &out) const {
  out << "var " << var;
  if (init)
    out << " = " << *init;
  return out << " : " << ty << ';';
}

std::ostream &Return::print(std::ostream &out) const {
  out << "return";
  if (arg)
    out << ' ' << *arg;
  return out << ';';
}

std::ostream &Callable::print(std::ostream &out) const {
  out << ( dynamic_cast<UNKNOWN * const >(return_ty) ? "proc " : "fun ");
  out << name << '(';
  for (auto const &p : args)
    out << p.first << " : " << p.second << ", ";
  out << ") ";
  if ( dynamic_cast<UNKNOWN * const >(return_ty) == NULL)
    out << " : " << return_ty << ' ';
  return out << *body;
}

std::ostream &GlobalVar::print(std::ostream &out) const {
  out << "var " << name;
  if (init)
    out << " = " << *init;
  return out << " : " << ty << ';';
}

std::ostream &operator<<(std::ostream &out, Program const &prog) {
  for (auto const &v : prog.global_vars)
    out << *v.second << '\n';
  for (auto const &cbl : prog.callables)
    out << *cbl.second << '\n';
  return out;
}

class ASTCreator {
public:
  Program read_program(BXParser::ProgramContext *ctx) {
    Program::CallTable callables;
    Program::GlobalVarTable global_vars;
    auto check_unique_name = [&](auto const &name) {
      if (global_vars.find(name) != global_vars.end())
        throw std::runtime_error("Redeclaration of existing global var " +
                                 name);
      if (callables.find(name) != callables.end())
        throw std::runtime_error("Redeclaration of existing callable " + name +
                                 "()");
    };
    for (auto child : ctx->children) {
      if (auto gv_ctx = dynamic_cast<BXParser::GlobalVarContext *>(child)) {
        for (auto &v : read_globalvar(gv_ctx)) {
          check_unique_name(v->name);
          global_vars.insert_or_assign(v->name, std::move(v));
        }
      } else if (auto proc_ctx = dynamic_cast<BXParser::ProcContext *>(child)) {
        auto c = read_proc(proc_ctx);
        check_unique_name(c->name);
        callables.insert_or_assign(c->name, std::move(c));
      } else if (auto func_ctx = dynamic_cast<BXParser::FuncContext *>(child)) {
        auto c = read_func(func_ctx);
        check_unique_name(c->name);
        callables.insert_or_assign(c->name, std::move(c));
      } else
        throw new std::runtime_error("Unknown top level declaration");
    }
    return Program{std::move(global_vars), std::move(callables)};
  }

private:
  std::vector<GlobalVarPtr> read_globalvar(BXParser::GlobalVarContext *ctx) {
    Type* ty = read_type(ctx->type());
    std::vector<GlobalVarPtr> vars;
    for (auto *gviCtx : ctx->globalVarInit()) {
      std::string name = gviCtx->ID()->getText();
      ExprPtr init;
      if (dynamic_cast<INT64 * const >(ty)){  
        init = read_num(gviCtx->NUM());
      }
      if (dynamic_cast<BOOL * const>(ty)){
        init = read_bool(gviCtx->BOOL());
      }
      if (dynamic_cast<POINTER * const>(ty)){
        init = read_num(gviCtx->NUM());
      }
      if (dynamic_cast<LIST * const>(ty)){
        init = read_num(gviCtx->NUM());
      }      
      vars.push_back(GlobalVar::make(name, ty, std::move(init)));
    }
    return vars;
  }

  CallablePtr read_proc(BXParser::ProcContext *ctx) {
    std::string name = ctx->ID()->getText();
    Callable::Params params;
    for (auto *param_ctx : ctx->parameter_groups()->param()) {
      for (auto &p : read_param(param_ctx))
        params.push_back(p);
    }
    BlockPtr body = read_block(ctx->block());
    return Callable::make(name, std::move(params), std::move(body), new UNKNOWN());
  }

  CallablePtr read_func(BXParser::FuncContext *ctx) {
    std::string name = ctx->ID()->getText();
    Callable::Params params;
    for (auto *param_ctx : ctx->parameter_groups()->param()) {
      for (auto &p : read_param(param_ctx))
        params.push_back(p);
    }
    BlockPtr body = read_block(ctx->block());
    return Callable::make(name, std::move(params), std::move(body),
                          read_type(ctx->type()));
  }

  Type* read_type(BXParser::TypeContext *ctx) {
    if (auto *int_ctx = dynamic_cast<BXParser::InttypeContext *>(ctx)){
      (void)int_ctx; 
      return new INT64();
    }
    if (auto *bool_ctx = dynamic_cast<BXParser::BooltypeContext *>(ctx)){
      (void)bool_ctx; 
      return new BOOL();
    }
    if (auto *pointer_ctx = dynamic_cast<BXParser::PointertypeContext *>(ctx)){
      return new POINTER(read_type(pointer_ctx->type()));
    }
    if (auto *list_ctx = dynamic_cast<BXParser::ListtypeContext*>(ctx)){
      return new LIST(read_type(list_ctx->type()), 
                      std::stoi(list_ctx->NUM()->getText()));
    }
  }

  std::vector<std::pair<std::string, Type*>>
  read_param(BXParser::ParamContext *ctx) {
    std::vector<std::pair<std::string, Type*>> params;
    Type* ty = read_type(ctx->type());
    for (auto *nm : ctx->ID())
      params.push_back(std::make_pair(nm->getText(), ty));
    return params;
  }

  std::vector<StmtPtr> read_stmt(BXParser::StmtContext *ctx) {
    std::vector<StmtPtr> stmts;
    if (auto *assign_ctx = dynamic_cast<BXParser::AssignContext *>(ctx))
      stmts.push_back(Assign::make(read_expr(assign_ctx->expr(0)),
                                   read_expr(assign_ctx->expr(1))));
    else if (auto *eval_ctx = dynamic_cast<BXParser::EvalContext *>(ctx))
      stmts.push_back(Eval::make(read_expr(eval_ctx->expr())));
    else if (auto *declare_ctx =
                 dynamic_cast<BXParser::DeclareContext *>(ctx)) {
      for (auto &d : read_declare(declare_ctx->varDecl()))
        stmts.push_back(std::move(d));
    } else if (auto *print_ctx = dynamic_cast<BXParser::PrintContext *>(ctx))
      stmts.push_back(Print::make(read_expr(print_ctx->expr())));
    else if (auto *scope_ctx = dynamic_cast<BXParser::ScopeContext *>(ctx))
      stmts.push_back(read_block(scope_ctx->block()));
    else if (auto *if_ctx = dynamic_cast<BXParser::IfContext *>(ctx))
      stmts.push_back(read_ifelse(if_ctx->ifElse()));
    else if (auto *while_ctx = dynamic_cast<BXParser::WhileContext *>(ctx))
      stmts.push_back(While::make(read_expr(while_ctx->expr()),
                                  read_block(while_ctx->block())));
    else if (auto *return_ctx = dynamic_cast<BXParser::ReturnContext *>(ctx))
      stmts.push_back(Return::make(read_optional_expr(return_ctx->expr())));
    else
      throw std::runtime_error{"Unknown statement type"};
    return stmts;
  }

  std::vector<StmtPtr> read_declare(BXParser::VarDeclContext *ctx) {
    std::vector<StmtPtr> decls;
    Type* ty = read_type(ctx->type());
    for (auto *vi : ctx->varInit()) {
      decls.push_back(
          Declare::make(vi->ID()->getText(), ty, read_expr(vi->expr())));
    }
    return decls;
  }

  IfElsePtr read_ifelse(BXParser::IfElseContext *ctx) {
    auto condition = read_expr(ctx->expr());
    auto then_block = read_block(ctx->block(0));
    StmtPtr else_block = Block::make();
    if (ctx->ifElse())
      else_block = read_ifelse(ctx->ifElse());
    else if (ctx->block(1))
      else_block = read_block(ctx->block(1));
    return IfElse::make(std::move(condition), std::move(then_block),
                        std::move(else_block));
  }

  BlockPtr read_block(BXParser::BlockContext *ctx) {
    std::vector<StmtPtr> body;
    for (auto *s_ctx : ctx->stmt())
      for (auto &s : read_stmt(s_ctx))
        body.push_back(std::move(s));
    return Block::make(body);
  }

  inline ExprPtr read_optional_expr(BXParser::ExprContext *ctx) {
    return ctx ? read_expr(ctx) : ExprPtr{nullptr};
  }

  ExprPtr read_expr(BXParser::ExprContext *ctx) {
    if (auto *alloc_ctx = dynamic_cast<BXParser::AllocContext *>(ctx)){
      return Alloc::make(read_expr(alloc_ctx->expr()), 
                  read_type(alloc_ctx->type()));
    }
    if (auto *null_ctx = dynamic_cast<BXParser::NullContext *>(ctx)){
      (void)null_ctx;
      return Null::make();
    }
    if (auto *deref_ctx = dynamic_cast<BXParser::DerefContext *>(ctx)){
      return Deref::make(read_expr(deref_ctx->expr()));
    }
    if (auto *addr_ctx = dynamic_cast<BXParser::AddressContext *>(ctx)){
      return Address::make(read_expr(addr_ctx->expr()));
    }
    if (auto *lelem_ctx = dynamic_cast<BXParser::ListelementContext *>(ctx)){
      return ListElem::make(read_expr(lelem_ctx->expr(0))
                            ,read_expr(lelem_ctx->expr(1)));
    }
    if (auto *variable_ctx = dynamic_cast<BXParser::IDContext *>(ctx))
      return Variable::make(variable_ctx->ID()->getText());
    else if (auto *call_ctx = dynamic_cast<BXParser::CallContext *>(ctx)) {
      std::vector<ExprPtr> args;
      for (auto *arg_ctx : call_ctx->expr())
        args.push_back(read_expr(arg_ctx));
      return Call::make(call_ctx->ID()->getText(), args);
    } else if (auto *number_ctx = dynamic_cast<BXParser::NumberContext *>(ctx))
      return read_num(number_ctx->NUM());
    else if (auto *bool_ctx = dynamic_cast<BXParser::BoolContext *>(ctx))
      return read_bool(bool_ctx->BOOL());
    else if (auto *unop_ctx = dynamic_cast<BXParser::UnopContext *>(ctx)) {
      auto op_txt = unop_ctx->op->getText();
      auto op = op_txt[0] == '~'
                    ? Unop::BitNot
                    : op_txt[0] == '-' ? Unop::Negate : Unop::LogNot;
      return UnopApp::make(op, read_expr(unop_ctx->expr()));
    } else if (auto *mul_ctx =
                   dynamic_cast<BXParser::MultiplicativeContext *>(ctx)) {
      auto op_txt = mul_ctx->op->getText();
      auto op = op_txt[0] == '*'
                    ? Binop::Multiply
                    : op_txt[0] == '/' ? Binop::Divide : Binop::Modulus;
      return BinopApp::make(read_expr(mul_ctx->expr(0)), op,
                            read_expr(mul_ctx->expr(1)));
    } else if (auto *add_ctx = dynamic_cast<BXParser::AdditiveContext *>(ctx)) {
      auto op_txt = add_ctx->op->getText();
      auto op = op_txt[0] == '+' ? Binop::Add : Binop::Subtract;
      return BinopApp::make(read_expr(add_ctx->expr(0)), op,
                            read_expr(add_ctx->expr(1)));
    } else if (auto *shift_ctx = dynamic_cast<BXParser::ShiftContext *>(ctx)) {
      auto op_txt = shift_ctx->op->getText();
      auto op = op_txt[0] == '<' ? Binop::Lshift : Binop::Rshift;
      return BinopApp::make(read_expr(shift_ctx->expr(0)), op,
                            read_expr(shift_ctx->expr(1)));
    } else if (auto *ineq_ctx =
                   dynamic_cast<BXParser::InequationContext *>(ctx)) {
      auto op_txt = ineq_ctx->op->getText();
      auto op = op_txt == "<"
                    ? Binop::Lt
                    : op_txt == "<=" ? Binop::Leq
                                     : op_txt == ">" ? Binop::Gt : Binop::Geq;
      return BinopApp::make(read_expr(ineq_ctx->expr(0)), op,
                            read_expr(ineq_ctx->expr(1)));
    } else if (auto *eq_ctx = dynamic_cast<BXParser::EquationContext *>(ctx)) {
      auto op_txt = eq_ctx->op->getText();
      auto op = op_txt[0] == '=' ? Binop::Eq : Binop::Neq;
      return BinopApp::make(read_expr(eq_ctx->expr(0)), op,
                            read_expr(eq_ctx->expr(1)));
    } else if (auto *band_ctx = dynamic_cast<BXParser::BitAndContext *>(ctx)) {
      return BinopApp::make(read_expr(band_ctx->expr(0)), Binop::BitAnd,
                            read_expr(band_ctx->expr(1)));
    } else if (auto *bxor_ctx = dynamic_cast<BXParser::BitXorContext *>(ctx)) {
      return BinopApp::make(read_expr(bxor_ctx->expr(0)), Binop::BitXor,
                            read_expr(bxor_ctx->expr(1)));
    } else if (auto *bor_ctx = dynamic_cast<BXParser::BitOrContext *>(ctx)) {
      return BinopApp::make(read_expr(bor_ctx->expr(0)), Binop::BitOr,
                            read_expr(bor_ctx->expr(1)));
    } else if (auto *land_ctx = dynamic_cast<BXParser::LogAndContext *>(ctx)) {
      return BinopApp::make(read_expr(land_ctx->expr(0)), Binop::BoolAnd,
                            read_expr(land_ctx->expr(1)));
    } else if (auto *lor_ctx = dynamic_cast<BXParser::LogOrContext *>(ctx)) {
      return BinopApp::make(read_expr(lor_ctx->expr(0)), Binop::BoolOr,
                            read_expr(lor_ctx->expr(1)));
    } else if (auto *parens_ctx = dynamic_cast<BXParser::ParensContext *>(ctx))
      return read_expr(parens_ctx->expr());
    else
      throw std::runtime_error{"Unknown expr type"};
  }

  ExprPtr read_num(antlr4::tree::TerminalNode *term) {
    return IntConstant::make(std::stoll(term->getText()));
  }

  ExprPtr read_bool(antlr4::tree::TerminalNode *term) {
    return BoolConstant::make(term->getText() == "true");
  }
};

Program read_program(std::string file) {
  std::ifstream stream;
  stream.open(file);
  antlr4::ANTLRInputStream input(stream);
  BXLexer lexer(&input);
  antlr4::CommonTokenStream tokens(&lexer);
  BXParser parser(&tokens);
  BXParser::ProgramContext *prog_ctx = parser.program();
  return ASTCreator{}.read_program(prog_ctx);
}

} // namespace source

} // namespace bx
