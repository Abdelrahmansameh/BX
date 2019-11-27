#include <stdexcept>

#include "amd64.h"
#include "ast_rtl.h"

namespace bx {

namespace rtl {

using source::Type;

int last_pseudo = 0;
inline rtl::Pseudo fresh_pseudo() { return rtl::Pseudo{last_pseudo++}; }

int last_label = 0;
inline rtl::Label fresh_label() { return rtl::Label{last_label++}; }

/**
 * List of global variable initializations
 */
std::map<std::string, int> global_var_init;

/**
 * Mapping from global variable to offset
 */
std::map<std::string, int> global_var_offset;

/**
 * Size of heap
 */
int globaloffset = 0;

/**
 * A common generator for both expressions and statements
 *
 * It could be possible to break this up into multiple generators
 * for statements, int64 expressions, boolean expressions, etc. with modest
 * increase in code complexity.
 */
struct RtlGen : public source::StmtVisitor,
                public source::ExprVisitor,
                public source::Addressor {
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
  /**
   * For assignables: address becomes a pseudo holding the address of the
   * assignable
   */
  rtl::Pseudo address{-1};

private:
  source::Program const &source_prog;
  rtl::Callable rtl_cbl;

  /**
   * Mapping from variables to pseudos
   */
  std::unordered_map<std::string, rtl::Pseudo> var_table;

  /**
   * Mapping from variables to offset
   */
  std::unordered_map<std::string, int> var_offset;

  /**
   * List of global variables
   */
  std::unordered_map<std::string, rtl::Pseudo> gvar_table;

  /*
   * Last offset
   */
  int lastoffset = 0;

  /**
   * Check if the variable is mapped to a pseudo; if not, create a fresh such
   * mapping. Return the pseudo in either case.
   */
  rtl::Pseudo get_pseudo(std::string const &v, int offset) {
    if (global_var_init.find(v) != global_var_init.end()) {
      if (gvar_table.find(v) == gvar_table.end()) {
        auto ps = fresh_pseudo();
        lastoffset += offset;
        add_sequential([&](auto next) {
          return Load::make(v, 0, ps, discard_pr, bx::amd64::reg::rip, next);
        });
        gvar_table.insert_or_assign(v, ps);
      }
      return gvar_table.at(v);
    }
    if (var_table.find(v) == var_table.end()) {
      var_table.insert_or_assign(v, fresh_pseudo());
      var_offset.insert_or_assign(v, lastoffset);
      lastoffset += offset;
    }
    return var_table.at(v);
  }

  rtl::Pseudo get_pseudo(source::Variable const &v) {
    return get_pseudo(v.label, source::sizeOf(v.meta->ty));
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
    lastoffset += 8;
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
    lastoffset += 8;
    add_sequential([&](auto next) { return Copy::make(result, reg, next); });
    return reg;
  }

public:
  RtlGen(source::Program const &source_prog, std::string const &name)
      : source_prog{source_prog}, rtl_cbl{name} {

    // Store the number of pseudos to computer later the number of tmps
    int pseudoCounter = last_pseudo;

    // Source callable
    auto &cbl = source_prog.callables.at(rtl_cbl.name);

    // input pseudos
    for (auto const &param : cbl->args) {
      Pseudo reg = get_pseudo(param.first, source::sizeOf(param.second));
      rtl_cbl.input_regs.push_back(reg);
    }

    // output pseudo
    if (dynamic_cast<source::UNKNOWN *>(cbl->return_ty)) {
      rtl_cbl.output_reg = rtl::discard_pr;
    } else {
      rtl_cbl.output_reg = fresh_pseudo();
      lastoffset += 8;
    }

    // enter label
    rtl_cbl.enter = fresh_label();
    lastoffset += 8;

    // leave label
    rtl_cbl.leave = fresh_label();
    lastoffset += 8;

    // Update in_label
    in_label = rtl_cbl.enter;

    // Placehold the new frame first
    auto fresh = fresh_label();
    lastoffset += 8;
    auto tmpin = in_label;
    in_label = fresh;

    // Save the callee saved registers O_o
    char const *callee_saved[] = {bx::amd64::reg::rbx, bx::amd64::reg::rbp,
                                  bx::amd64::reg::r12, bx::amd64::reg::r13,
                                  bx::amd64::reg::r14, bx::amd64::reg::r15};
    std::vector<rtl::Pseudo> saved_locs;
    for (int i = 0; i < 6; i++) {
      auto ps = fresh_pseudo();
      lastoffset += 8;
      saved_locs.push_back(ps);
      add_sequential(
          [&](auto next) { return CopyMP::make(callee_saved[i], ps, next); });
    }

    // Retrieve the arguments
    int nArgs = static_cast<int>(cbl->args.size());
    char const *regargs[] = {bx::amd64::reg::rdi, bx::amd64::reg::rsi,
                             bx::amd64::reg::rdx, bx::amd64::reg::rcx,
                             bx::amd64::reg::r8,  bx::amd64::reg::r9};
    bool stackParams = nArgs > 6;
    if (!stackParams) {
      for (int i = 0; i < nArgs; i++) {
        add_sequential([&](auto next) {
          return CopyMP::make(regargs[i], rtl_cbl.input_regs[i], next);
        });
      }
    } else {
      for (int i = 0; i < 6; i++) {
        add_sequential([&](auto next) {
          return CopyMP::make(regargs[i], rtl_cbl.input_regs[i], next);
        });
      }
      for (int i = 6; i < nArgs + 1; i++) {
        add_sequential([&](auto next) {
          return LoadParam::make(i - 5, rtl_cbl.input_regs[i], next);
        });
      }
    }
    // Process all the statements
    cbl->body->accept(*this);

    // Put the return value in rax
    if (!dynamic_cast<source::UNKNOWN *>(cbl->return_ty)) {
      add_sequential([&](auto next) {
        return CopyPM::make(rtl_cbl.output_reg, bx::amd64::reg::rax, next);
      });
    }

    rtl_cbl.add_instr(rtl_cbl.leave, Goto::make(in_label));
    // Restore the calle saved registers
    for (int i = 0; i < 6; i++) {
      add_sequential([&](auto next) {
        return CopyPM::make(saved_locs[i], callee_saved[i], next);
      });
    }
    // Update the size of NewFrame
    pseudoCounter -= last_pseudo;
    rtl_cbl.add_instr(tmpin, NewFrame::make(fresh, lastoffset));

    // Insert a Delframe
    // rtl_cbl.add_instr(in_label, DelFrame::make(rtl_cbl.leave));
    add_sequential([&](auto next) { return DelFrame::make(next); });

    // Return
    // rtl_cbl.add_instr(rtl_cbl.leave, Return::make());
    add_sequential([&](auto next) {
      (void)next; // suppress unused warning
      return Return::make();
    });
  }

  rtl::Callable &&deliver() { return std::move(rtl_cbl); }

  void addMemset(int offset, int size) {
    //auto offset = lastoffset;
    /*auto rbp = fresh_pseudo();
    lastoffset += 8;
    add_sequential([&](auto next) {
      return CopyMP::make(bx::amd64::reg::rbp, rbp, next);
    });
    auto ioffset = source::IntConstant::make(offset);
    ioffset->accept(*this);
    auto poffset = result;
    add_sequential([&](auto next) {
      return Binop::make(rtl::Binop::SUB, rbp, poffset, next);
    });*/
    auto poffset = fresh_pseudo();
    lastoffset += 8;
    add_sequential([&](auto next) {
        return CopyAP::make("", -offset, bx::amd64::reg::rbp, discard_pr, poffset, next);
    });
    auto isize = source::IntConstant::make(size);
    isize->accept(*this);
    auto psize = result;
    auto izero = source::IntConstant::make(0);
    izero->accept(*this);
    auto pzero = result;
    // rdi rsi rdx
    add_sequential([&](auto next) {
      return CopyPM::make(poffset, bx::amd64::reg::rdi, next);
    });
    add_sequential([&](auto next) {
      return CopyPM::make(pzero, bx::amd64::reg::rsi, next);
    });
    add_sequential([&](auto next) {
      return CopyPM::make(psize, bx::amd64::reg::rdx, next);
    });
    add_sequential([&](auto next) { return Call::make("memset", 3, next); });
  }
  void visit(source::Declare const &dec) override {
    if (dynamic_cast<source::BOOL *>(dec.ty)) {
      auto pr = get_pseudo(dec.var, 8);
      dec.init->accept(*this);
      intify();
      add_sequential([&](auto next) { return Copy::make(result, pr, next); });
    }
    if (dynamic_cast<source::INT64 *>(dec.ty)) {
      auto pr = get_pseudo(dec.var, 8);
      dec.init->accept(*this);
      add_sequential([&](auto next) { return Copy::make(result, pr, next); });
    }
    if (dynamic_cast<source::POINTER *>(dec.ty)) {
      auto pr = get_pseudo(dec.var, 8);
      dec.init->accept(*this);
      add_sequential([&](auto next) { return Copy::make(result, pr, next); });
    }
    if (auto lst = dynamic_cast<source::LIST *>(dec.ty)) {
      auto pr = get_pseudo(dec.var, source::sizeOf(lst));
      std::cout << pr << std::endl;
      addMemset(var_offset.at(dec.var), source::sizeOf(lst));
      dec.init->accept(*this);
      add_sequential([&](auto next) { return Copy::make(result, pr, next);
      });
    }
  }

  void visit(source::Assign const &mv) override {
    mv.left->acceptAddress(*this);
    auto source_reg = address;
    mv.right->accept(*this);
    if (dynamic_cast<source::BOOL *>(mv.right->meta->ty))
      intify();
    add_sequential([&](auto next) {
      return Store::make(result, "", source_reg, bx::amd64::reg::rbp, 0, next);
    });
    /*if (gvar_table.find(mv.left) == gvar_table.end()){
      add_sequential(
        [&](auto next) { return Copy::make(result, source_reg, next); });
    }
    else{
      add_sequential(
        [&](auto next) { return Store::make(result, mv.left, 0, next );} );
      add_sequential(
        [&](auto next) { return Copy::make(result, source_reg, next); });
    }*/
  }

  void visit(source::Eval const &ev) override {
    ev.expr->accept(*this);
    if (dynamic_cast<source::BOOL *>(ev.expr->meta->ty))
      intify();
  }

  void visit(source::Print const &pr) override {
    pr.arg->accept(*this);
    if (dynamic_cast<source::BOOL *>(pr.arg->meta->ty))
      intify();
    std::string func = dynamic_cast<source::INT64 *>(pr.arg->meta->ty)
                           ? "bx_print_int"
                           : "bx_print_bool";
    add_sequential([&](auto next) {
      return CopyPM::make(result, bx::amd64::reg::rdi, next);
    });
    add_sequential([&](auto next) { return Call::make(func, 1, next); });
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
      if (dynamic_cast<source::BOOL *>(ret.arg->meta->ty))
        intify();
      if (rtl_cbl.output_reg != rtl::discard_pr) {
        add_sequential([&](auto next) {
          return Copy::make(result, rtl_cbl.output_reg, next);
        });
        // Put the return value in rax
        add_sequential([&](auto next) {
          return CopyPM::make(rtl_cbl.output_reg, bx::amd64::reg::rax, next);
        });
      }
    }
    add_sequential([&](auto next) {
      (void)next; // suppress unused warning
      return Goto::make(rtl_cbl.leave);
    });
  }

  void visit(source::Variable const &v) override {
    result = get_pseudo(v);
    if (dynamic_cast<source::BOOL *>(v.meta->ty)) {
      false_label = fresh_label();
      add_sequential([&](auto next) {
        return Ubranch::make(rtl::Ubranch::JNZ, result, next, false_label);
      });
    }
  }

  void visit(source::IntConstant const &k) override {
    result = fresh_pseudo();
    lastoffset += 8;
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
    if (dynamic_cast<source::BOOL *>(bo.left_arg->meta->ty))
      intify();
    auto left_result = result;
    bo.right_arg->accept(*this);
    if (dynamic_cast<source::BOOL *>(bo.right_arg->meta->ty))
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
    int nArgs = static_cast<int>(args.size());
    char const *regargs[] = {bx::amd64::reg::rdi, bx::amd64::reg::rsi,
                             bx::amd64::reg::rdx, bx::amd64::reg::rcx,
                             bx::amd64::reg::r8,  bx::amd64::reg::r9};
    bool stackParams = nArgs > 6;
    if (!stackParams) {
      for (int i = 0; i < nArgs; i++) {
        add_sequential(
            [&](auto next) { return CopyPM::make(args[i], regargs[i], next); });
      }
    } else {
      for (int i = 0; i < 6; i++) {
        add_sequential(
            [&](auto next) { return CopyPM::make(args[i], regargs[i], next); });
      }
      for (int i = 0; i < nArgs - 5; i++) {
        add_sequential(
            [&](auto next) { return Push::make(args[nArgs - i], next); });
      }
    }
    if (dynamic_cast<source::UNKNOWN *>(
            source_prog.callables.at(ca.func)->return_ty)) {
      result = rtl::discard_pr;
    } else {
      result = fresh_pseudo();
      lastoffset += 8;
    }
    add_sequential([&](auto next) { return Call::make(ca.func, nArgs, next); });
    if (!dynamic_cast<source::UNKNOWN *>(
            source_prog.callables.at(ca.func)->return_ty)) {
      add_sequential([&](auto next) {
        return CopyMP::make(bx::amd64::reg::rax, result, next);
      });
    }
  }

  void visit(source::Alloc const &al) override {
    auto iscale = source::IntConstant::make(sizeOf(al.typ));
    iscale->accept(*this);
    auto scale = result;
    al.size->accept(*this);
    auto length = result;
    add_sequential([&](auto next) {
      return Binop::make(rtl::Binop::MUL, scale, length, next);
    });
    add_sequential([&](auto next) {
      return CopyPM::make(length, bx::amd64::reg::rdi, next);
    });
    std::string func = "malloc";
    add_sequential([&](auto next) { return Call::make(func, 1, next); });
    auto ps = fresh_pseudo();
    lastoffset += 8;
    add_sequential(
        [&](auto next) { return CopyMP::make(bx::amd64::reg::rax, ps, next); });
    result = ps;
  }

  void visit(source::Null const &nl) override {
    auto nul = source::IntConstant::make(0);
    nul->accept(*this);
  }

  void visit(source::Address const &adr) override {
    adr.src->acceptAddress(*this);
    result = address;
  }

  void visit(source::ListElem const &lelm) override {
    lelm.lst->acceptAddress(*this);
    auto lstaddress = address;
    lelm.idx->accept(*this);
    auto idx = result;
    source::IntConstantPtr iscale;
    if (auto lst = dynamic_cast<source::LIST *>(lelm.lst->meta->ty)) {
      iscale = source::IntConstant::make(sizeOf(lst->typ));
    }
    iscale->accept(*this);
    auto scale = result;
    add_sequential([&](auto next) {
      return Binop::make(rtl::Binop::MUL, scale, idx, next);
    });
    add_sequential([&](auto next) {
      return Binop::make(rtl::Binop::SUB, idx, lstaddress, next);
    });
    auto ps = fresh_pseudo();
    lastoffset += 8;
    add_sequential([&](auto next) {
      return Load::make("", 0, ps, lstaddress, bx::amd64::reg::rip, next);
    });
    result = ps;
  }

  void visit(source::Deref const &drf) override {
    drf.ptr->acceptAddress(*this);
    auto ps = fresh_pseudo();
    lastoffset += 8;
    add_sequential([&](auto next) {
      return Load::make("", 0, ps, address, bx::amd64::reg::rip, next);
    });
    result = ps;
  }

  void visitAddress(source::Variable const &va) override {
    auto v = va.label;
    if (global_var_init.find(v) != global_var_init.end()) {
      auto ps = fresh_pseudo();
      lastoffset += 8;
      add_sequential([&](auto next) {
        return CopyAP::make(v, -1, bx::amd64::reg::rip, discard_pr, ps, next);
      });
      address = ps;
    }
    if (var_offset.find(v) != var_offset.end()) {
      auto ps = fresh_pseudo();
      lastoffset += 8;
      add_sequential([&](auto next) {
        return CopyAP::make("", -var_offset.at(v), bx::amd64::reg::rbp,
                            discard_pr, ps, next);
      });
      address = ps;
    }
  }

  void visitAddress(source::ListElem const &lelm) override {
    lelm.lst->acceptAddress(*this);
    auto tmpaddr = address;
    lelm.idx->accept(*this);
    auto tmpidx = result;
    source::IntConstantPtr ioffset;
    if (auto lst = dynamic_cast<source::LIST *>(lelm.lst->meta->ty)) {
      ioffset = source::IntConstant::make(source::sizeOf(lst->typ));
    }
    ioffset->accept(*this);
    add_sequential([&](auto next) {
      return Binop::make(Binop::MUL, result, tmpidx, next);
    });
    add_sequential([&](auto next) {
      return Binop::make(Binop::SUB, tmpidx, tmpaddr, next);
    });
    auto ps = fresh_pseudo();
    lastoffset += 8;
    add_sequential([&](auto next) {
      return CopyAP::make("", 0, bx::amd64::reg::rip, tmpaddr, ps, next);
    });
    address = ps;
  }

  void visitAddress(source::Deref const &drf) override {
    drf.ptr->acceptAddress(*this);
    auto ps = fresh_pseudo();
    lastoffset += 8;
    add_sequential([&](auto next) {
      return Load::make("", 0, ps, address, bx::amd64::reg::rbp, next);
    });
    address = ps;
  }
};

std::map<std::string, int> getGlobals(source::Program const &src_prog) {
  for (auto &glb : src_prog.global_vars) {
    // Seperated the cases for debgging
    if (dynamic_cast<source::INT64 *>(glb.second->ty) ||
        dynamic_cast<source::BOOL *>(glb.second->ty)) {
      int *init = glb.second->init->getArg();
      if (init == NULL) {
        std::cout << "Bad variable initialization for " << glb.first
                  << std::endl;
      } else {
        global_var_init.insert(std::pair<std::string, int>(glb.first, *init));
        global_var_offset.insert(
            std::pair<std::string, int>(glb.first, globaloffset));
        globaloffset += bx::source::sizeOf(glb.second->ty);
      }
    }
    if (dynamic_cast<source::POINTER *>(glb.second->ty)) {
      int *init = glb.second->init->getArg();
      if (init == NULL) {
        std::cout << "Bad variable initialization for " << glb.first
                  << std::endl;
      } else {
        global_var_init.insert(std::pair<std::string, int>(glb.first, *init));
        global_var_offset.insert(
            std::pair<std::string, int>(glb.first, globaloffset));
        globaloffset += bx::source::sizeOf(glb.second->ty);
      }
    }
    if (dynamic_cast<source::LIST *>(glb.second->ty)) {
      int *init = glb.second->init->getArg();
      if (init == NULL) {
        std::cout << "Bad variable initialization for " << glb.first
                  << std::endl;
      } else {
        global_var_init.insert(std::pair<std::string, int>(glb.first, *init));
        global_var_offset.insert(
            std::pair<std::string, int>(glb.first, globaloffset));
        globaloffset += bx::source::sizeOf(glb.second->ty);
      }
    }
  }
  return global_var_init;
}

rtl::Program transform(source::Program const &src_prog) {
  rtl::Program rtl_prog;
  for (auto const &cbl : src_prog.callables) {
    RtlGen gen{src_prog, cbl.first};
    rtl_prog.push_back(gen.deliver());
  }
  return rtl_prog;
  // return std::make_pair(rtl_prog, global_var_init);
}

} // namespace rtl

} // namespace bx
