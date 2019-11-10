#include <stdexcept>

#include "ast_rtl.h"
#include "amd64.h"

namespace bx {

namespace rtl {

using source::Type;

int last_pseudo = 0;
inline rtl::Pseudo fresh_pseudo() { return rtl::Pseudo{last_pseudo++}; }

int last_label = 0;
inline rtl::Label fresh_label() { return rtl::Label{last_label++}; }

/**
 * A common generator for both expressions and statements
 *
 * It could be possible to break this up into multiple generators
 * for statements, int64 expressions, boolean expressions, etc. with modest
 * increase in code complexity.
 */
struct RtlGen : public source::StmtVisitor, public source::ExprVisitor {
  /** input label where "next" instruction will be
   *
   * After code gen:
   *
   *   - for int64 expressions and statements: in_label becomes location of
   *     next instruction
   *
   *   - for bool expressions: in_label becomes location of true branch
   */
  rtl::Label in_label;
  /**
   * For boolean expressions: false_label becomes location of false branch
   */
  rtl::Label false_label{-1};
  /**
   * For int64 expressions: result becomes destination for value
   */
  rtl::Pseudo result{-1};

private:
  source::Program const &source_prog;
  rtl::Callable rtl_cbl;

  /**
   * Mapping from variables to pseudos
   */
  std::unordered_map<std::string, rtl::Pseudo> var_table;

  /**
   * Check if the variable is mapped to a pseudo; if not, create a fresh such
   * mapping. Return the pseudo in either case.
   */
  rtl::Pseudo get_pseudo(std::string const &v) {
    if (var_table.find(v) == var_table.end())
      var_table.insert_or_assign(v, fresh_pseudo());
    return var_table.at(v);
  }

  rtl::Pseudo get_pseudo(source::Variable const &v) {
    return get_pseudo(v.label);
  }

  /**
   * Add an instruction by generating a next label, and then updating in_label
   * to that next label.
   *
   * @param use_label A function that creates an Instr* using the generated next
   * label
   */
  template <typename LabelUser>
  inline void add_sequential(LabelUser use_label) {
    auto next_label = fresh_label();
    rtl_cbl.add_instr(in_label, use_label(next_label));
    in_label = next_label;
  }

  /**
   * Force the bool result into an int64 result
   */
  void intify() {
    result = fresh_pseudo();
    auto next_label = fresh_label();
    rtl_cbl.add_instr(in_label, Move::make(1, result, next_label));
    rtl_cbl.add_instr(false_label, Move::make(0, result, next_label));
    in_label = next_label;
  }

  /**
   * Get a fresh copy of the result to avoid clobbering it
   */
  rtl::Pseudo copy_of_result() {
    auto reg = fresh_pseudo();
    add_sequential([&](auto next) { return Copy::make(result, reg, next); });
    return reg;
  }

public:
  RtlGen(source::Program const &source_prog, std::string const &name)
      : source_prog{source_prog}, rtl_cbl{name} {
    auto &cbl = source_prog.callables.at(rtl_cbl.name);
    // input pseudos
    for (auto const &param : cbl->args) {
      Pseudo reg = get_pseudo(param.first);
      rtl_cbl.input_regs.push_back(reg);
    }
    // output pseudo
    rtl_cbl.output_reg =
        cbl->return_ty == Type::UNKNOWN ? rtl::discard_pr : fresh_pseudo();
    // enter and leave labels
    rtl_cbl.enter = fresh_label();
    rtl_cbl.leave = fresh_label();
    // process all the statements
    in_label = rtl_cbl.enter;
    cbl->body->accept(*this);
    // add an unconditional jump to exit for procedures
    if (cbl->return_ty == Type::UNKNOWN)
      rtl_cbl.add_instr(in_label, Goto::make(rtl_cbl.leave));
    //Put return value in RDX
    auto fresh1 = fresh_label();
    auto fresh2 = fresh_label();
    rtl_cbl.add_instr(rtl_cbl.leave, CopyPM::make(rtl_cbl.output_reg, bx::amd64::reg::rax, fresh1));
    rtl_cbl.add_instr(fresh1, DelFrame::make(fresh2));
    rtl_cbl.add_instr(fresh2, Return::make());
  }

  rtl::Callable &&deliver() { return std::move(rtl_cbl); }

  void visit(source::Declare const &dec) override {
    auto pr = get_pseudo(dec.var);
    dec.init->accept(*this);
    if (dec.ty == Type::BOOL)
      intify();
    add_sequential([&](auto next) { return Copy::make(result, pr, next); });
  }

  void visit(source::Assign const &mv) override {
    auto source_reg = get_pseudo(mv.left);
    mv.right->accept(*this);
    if (mv.right->meta->ty == Type::BOOL)
      intify();
    add_sequential(
        [&](auto next) { return Copy::make(result, source_reg, next); });
  }

  void visit(source::Eval const &ev) override {
    ev.expr->accept(*this);
    if (ev.expr->meta->ty == Type::BOOL)
      intify();
  }

  void visit(source::Print const &pr) override {
    pr.arg->accept(*this);
    if (pr.arg->meta->ty == Type::BOOL)
      intify();
    std::string func =
        pr.arg->meta->ty == Type::INT64 ? "bx_print_int" : "bx_print_bool";
    add_sequential([&](auto next) {
      return Call::make(func, std::vector<Pseudo>{result}, rtl::discard_pr,
                        next);
    });
  }

  void visit(source::Block const &bl) override {
    for (auto const &stmt : bl.body)
      stmt->accept(*this);
  }

  void visit(source::IfElse const &ie) override {
    ie.condition->accept(*this);
    // save a copy of the outputs
    auto then_label = in_label, else_label = false_label;
    auto next_label = fresh_label();
    // put the then-block at the then_label
    in_label = then_label;
    ie.true_branch->accept(*this);
    rtl_cbl.add_instr(in_label, Goto::make(next_label));
    // put the else-block at the else_label
    in_label = else_label;
    ie.false_branch->accept(*this);
    rtl_cbl.add_instr(in_label, Goto::make(next_label));
    // now both branches have reached next_label
    in_label = next_label;
  }

  void visit(source::While const &wh) override {
    // save a copy of the while loop enter
    auto while_enter_label = in_label;
    wh.condition->accept(*this);
    // save a copy of the false_label as that is the ultimate exit label
    auto condition_exit_label = false_label;
    wh.loop_body->accept(*this);
    rtl_cbl.add_instr(in_label, Goto::make(while_enter_label));
    in_label = condition_exit_label;
  }

  void visit(source::Return const &ret) override {
    if (ret.arg) {
      ret.arg->accept(*this);
      if (ret.arg->meta->ty == Type::BOOL)
        intify();
      if (rtl_cbl.output_reg != rtl::discard_pr)
        add_sequential([&](auto next) {
          return Copy::make(result, rtl_cbl.output_reg, next);
        });
    }
    add_sequential([&](auto next) {
      (void)next; // suppress unused warning
      return Goto::make(rtl_cbl.leave);
    });
  }

  void visit(source::Variable const &v) override {
    result = get_pseudo(v);
    if (v.meta->ty == Type::BOOL) {
      false_label = fresh_label();
      add_sequential([&](auto next) {
        return Ubranch::make(rtl::Ubranch::JNZ, result, next, false_label);
      });
    }
  }

  void visit(source::IntConstant const &k) override {
    result = fresh_pseudo();
    add_sequential(
        [&](auto next_lab) { return Move::make(k.value, result, next_lab); });
  }

  void visit(source::BoolConstant const &k) override {
    if (k.value) {
      // in_label does not change
      false_label = fresh_label();
    } else {
      false_label = in_label;
      in_label = fresh_label();
    }
  }

  void visit(source::UnopApp const &uo) override {
    uo.arg->accept(*this);
    switch (uo.op) {
    case source::Unop::BitNot:
    case source::Unop::Negate: {
      result = copy_of_result();
      auto rtl_op =
          uo.op == source::Unop::BitNot ? rtl::Unop::NOT : rtl::Unop::NEG;
      add_sequential(
          [&](auto next) { return Unop::make(rtl_op, result, next); });
    } break;
    case source::Unop::LogNot: {
      auto l = false_label;
      false_label = in_label;
      in_label = l;
    } break;
    default:
      throw std::runtime_error("Cannot compile unary operator");
      break;
    }
  }

  void visitIntBinop(source::BinopApp const &bo) {
    rtl::Binop::Code rtl_op;
    // clang-format off
    switch (bo.op) {
    case source::Binop::Add:      rtl_op = rtl::Binop::ADD; break;
    case source::Binop::Subtract: rtl_op = rtl::Binop::SUB; break;
    case source::Binop::Multiply: rtl_op = rtl::Binop::MUL; break;
    case source::Binop::Divide:   rtl_op = rtl::Binop::DIV; break;
    case source::Binop::Modulus:  rtl_op = rtl::Binop::REM; break;
    case source::Binop::BitAnd:   rtl_op = rtl::Binop::AND; break;
    case source::Binop::BitOr:    rtl_op = rtl::Binop::OR;  break;
    case source::Binop::BitXor:   rtl_op = rtl::Binop::XOR; break;
    case source::Binop::Lshift:   rtl_op = rtl::Binop::SAL; break;
    case source::Binop::Rshift:   rtl_op = rtl::Binop::SAR; break;
    default: return; // case not relevant
    }
    // clang-format on
    bo.left_arg->accept(*this);
    auto left_result = copy_of_result();
    bo.right_arg->accept(*this);
    auto right_result = result;
    add_sequential([&](auto next) {
      return Binop::make(rtl_op, right_result, left_result, next);
    });
    result = left_result;
  }

  void visitBoolBinop(source::BinopApp const &bo) {
    if (bo.op != source::Binop::BoolAnd && bo.op != source::Binop::BoolOr)
      return; // case not relevant
    bo.left_arg->accept(*this);
    auto left_true_label = in_label, left_false_label = false_label;
    in_label =
        bo.op == source::Binop::BoolAnd ? left_true_label : left_false_label;
    bo.right_arg->accept(*this);
    auto right_true_label = in_label, right_false_label = false_label;
    if (bo.op == source::Binop::BoolAnd) {
      rtl_cbl.add_instr(right_false_label, Goto::make(left_false_label));
      false_label = left_false_label;
    } else {
      rtl_cbl.add_instr(right_true_label, Goto::make(left_true_label));
      in_label = left_true_label;
    }
  }

  void visitIneqop(source::BinopApp const &bo) {
    rtl::Bbranch::Code rtl_op;
    // clang-format off
    switch (bo.op) {
    case source::Binop::Lt:  rtl_op = rtl::Bbranch::JL;  break;
    case source::Binop::Leq: rtl_op = rtl::Bbranch::JLE; break;
    case source::Binop::Gt:  rtl_op = rtl::Bbranch::JG;  break;
    case source::Binop::Geq: rtl_op = rtl::Bbranch::JGE; break;
    default: return; // case not relevant
    }
    // clang-format on
    bo.left_arg->accept(*this);
    auto left_result = result; // save
    bo.right_arg->accept(*this);
    auto right_result = result; // save
    false_label = fresh_label();
    add_sequential([&](auto next) {
      return Bbranch::make(rtl_op, left_result, right_result, next,
                           false_label);
    });
  }

  void visitEqop(source::BinopApp const &bo) {
    if (bo.op != source::Binop::Eq && bo.op != source::Binop::Neq)
      return; // case not relevant
    bo.left_arg->accept(*this);
    if (bo.left_arg->meta->ty == Type::BOOL)
      intify();
    auto left_result = result;
    bo.right_arg->accept(*this);
    if (bo.right_arg->meta->ty == Type::BOOL)
      intify();
    false_label = fresh_label();
    auto bbr_op =
        bo.op == source::Binop::Eq ? rtl::Bbranch::JE : rtl::Bbranch::JNE;
    add_sequential([&](auto next) {
      return Bbranch::make(bbr_op, left_result, result, next, false_label);
    });
  }

  void visit(source::BinopApp const &bo) override {
    // try all four visits; at most one of them will work
    visitIntBinop(bo);
    visitBoolBinop(bo);
    visitIneqop(bo);
    visitEqop(bo);
  }

  void visit(source::Call const &ca) override {
    std::vector<Pseudo> args;
    for (auto const &e : ca.args) {
      e->accept(*this);
      args.push_back(result);
    }
    result = source_prog.callables.at(ca.func)->return_ty == Type::UNKNOWN
                 ? rtl::discard_pr
                 : fresh_pseudo();
    add_sequential(
        [&](auto next) { return Call::make(ca.func, args, result, next); });
  }
};

rtl::Program transform(source::Program const &src_prog) {
  rtl::Program rtl_prog;
  for (auto const &cbl : src_prog.callables) {
    RtlGen gen{src_prog, cbl.first};
    rtl_prog.push_back(gen.deliver());
  }
  return rtl_prog;
}

} // namespace rtl

} // namespace bx
