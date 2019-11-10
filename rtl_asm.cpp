/**
 * This file transforms RTL to AMD64 assembly
 *
 * Classes:
 *
 *     bx::InstrCompiler:
 *         A visitor that compiles bx::rtl::Instr one by one
 *
 *  Functions
 *
 *     AsmProgram bx::rtl_to_asm(rtl::Program const &prog)
 *         The main compilation function
 */

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>

#include "amd64.h"
#include "rtl.h"
#include "rtl_asm.h"

namespace bx {

using namespace amd64;

class InstrCompiler : public rtl::InstrVisitor {
private:
  std::string funcname, exit_label;
  std::unordered_map<int, amd64::Pseudo> rmap{};
  AsmProgram body{};

  amd64::Pseudo lookup(rtl::Pseudo r) {
    if (rmap.find(r.id) == rmap.end()) {
      amd64::Pseudo p{static_cast<int>(rmap.size() + 1)};
      rmap.insert({r.id, p});
    }
    return rmap.at(r.id);
  }

  amd64::Label label_translate(rtl::Label const &rtl_lab) {
    return std::string{".L"} + funcname + '.' + std::to_string(rtl_lab.id);
  }

  void append(std::unique_ptr<Asm> line) { body.push_back(std::move(line)); }

public:
  void append_label(rtl::Label const &rtl_lab) {
    std::string label = label_translate(rtl_lab);
    if (body.size() > 0 && body.back()->repr_template.rfind("\tjmp", 0) == 0 &&
        body.back()->jump_dests.size() > 0 &&
        body.back()->jump_dests[0] == label)
      body.pop_back(); // get rid of a redundant jmp;
    append(Asm::set_label(label));
  }

  InstrCompiler(std::string funcname)
      : funcname{funcname}, exit_label{".L" + funcname + ".exit"} {}

  AsmProgram finalize() {
    AsmProgram prog;
    prog.push_back(Asm::directive(".globl " + funcname));
    prog.push_back(Asm::directive(".section .text"));
    prog.push_back(Asm::set_label(funcname));
    if (rmap.size() > 0) {
      prog.push_back(Asm::pushq(Pseudo{reg::rbp}));
      prog.push_back(Asm::movq(Pseudo{reg::rsp}, Pseudo{reg::rbp}));
      prog.push_back(Asm::subq(rmap.size() * 8, Pseudo{reg::rsp}));
    }
    for (auto i = body.begin(), e = body.end(); i != e; i++)
      prog.push_back(std::move(*i));
    prog.push_back(Asm::set_label(exit_label));
    if (rmap.size() > 0) {
      prog.push_back(Asm::movq(Pseudo{reg::rbp}, Pseudo{reg::rsp}));
      prog.push_back(Asm::popq(Pseudo{reg::rbp}));
    }
    prog.push_back(Asm::ret());
    return prog;
  }

  void visit(rtl::Move const &mv) override {
    int64_t src = mv.source;
    if (src < INT32_MIN || src > INT32_MAX)
      append(Asm::movabsq(src, lookup(mv.dest)));
    else
      append(Asm::movq(src, lookup(mv.dest)));
    append(Asm::jmp(label_translate(mv.succ)));
  }

  void visit(rtl::Copy const &cp) override {
    append(Asm::movq(lookup(cp.src), Pseudo{reg::rax}));
    append(Asm::movq(Pseudo{reg::rax}, lookup(cp.dest)));
    append(Asm::jmp(label_translate(cp.succ)));
  }

  void visit(rtl::Binop const &bo) override {
    auto src = lookup(bo.src);
    auto dest = lookup(bo.dest);
    append(Asm::movq(dest, Pseudo{reg::rax}));
    switch (bo.opcode) {
    case rtl::Binop::ADD:
      append(Asm::addq(src, Pseudo{reg::rax}));
      append(Asm::movq(Pseudo{reg::rax}, dest));
      break;
    case rtl::Binop::SUB:
      append(Asm::subq(src, Pseudo{reg::rax}));
      append(Asm::movq(Pseudo{reg::rax}, dest));
      break;
    case rtl::Binop::AND:
      append(Asm::andq(src, Pseudo{reg::rax}));
      append(Asm::movq(Pseudo{reg::rax}, dest));
      break;
    case rtl::Binop::OR:
      append(Asm::orq(src, Pseudo{reg::rax}));
      append(Asm::movq(Pseudo{reg::rax}, dest));
      break;
    case rtl::Binop::XOR:
      append(Asm::xorq(src, Pseudo{reg::rax}));
      append(Asm::movq(Pseudo{reg::rax}, dest));
      break;
    case rtl::Binop::MUL:
      append(Asm::imulq(src));
      append(Asm::movq(Pseudo{reg::rax}, dest));
      break;
    case rtl::Binop::DIV:
      append(Asm::cqo());
      append(Asm::idivq(src));
      append(Asm::movq(Pseudo{reg::rax}, dest));
      break;
    case rtl::Binop::REM:
      append(Asm::cqo());
      append(Asm::idivq(src));
      append(Asm::movq(Pseudo{reg::rdx}, dest));
      break;
    case rtl::Binop::SAL:
      append(Asm::movq(src, Pseudo{reg::rcx}));
      append(Asm::salq(dest));
      break;
    case rtl::Binop::SAR:
      append(Asm::movq(src, Pseudo{reg::rcx}));
      append(Asm::sarq(dest));
      break;
    }
    append(Asm::jmp(label_translate(bo.succ)));
  }

  void visit(rtl::Unop const &uo) override {
    Pseudo arg = lookup(uo.arg);
    switch (uo.opcode) {
    case rtl::Unop::NEG:
      append(Asm::negq(arg));
      break;
    case rtl::Unop::NOT:
      append(Asm::notq(arg));
      break;
    }
    append(Asm::jmp(label_translate(uo.succ)));
  }

  void visit(rtl::Ubranch const &ub) override {
    Pseudo arg = lookup(ub.arg);
    append(Asm::cmpq(0u, arg));
    switch (ub.opcode) {
    case rtl::Ubranch::JZ:
      append(Asm::je(label_translate(ub.succ)));
      break;
    case rtl::Ubranch::JNZ:
      append(Asm::jne(label_translate(ub.succ)));
      break;
    }
    append(Asm::jmp(label_translate(ub.fail)));
  }

  void visit(rtl::Bbranch const &bb) override {
    Pseudo arg1 = lookup(bb.arg1);
    Pseudo arg2 = lookup(bb.arg2);
    append(Asm::movq(arg1, Pseudo{reg::rcx}));
    append(Asm::movq(arg2, Pseudo{reg::rax}));
    append(Asm::cmpq(Pseudo{reg::rax}, Pseudo{reg::rcx}));
    switch (bb.opcode) {
    case rtl::Bbranch::JE:
      append(Asm::jne(label_translate(bb.fail)));
      break;
    case rtl::Bbranch::JNE:
      append(Asm::je(label_translate(bb.fail)));
      break;
    case rtl::Bbranch::JL:
    case rtl::Bbranch::JNGE:
      append(Asm::jge(label_translate(bb.fail)));
      break;
    case rtl::Bbranch::JLE:
    case rtl::Bbranch::JNG:
      append(Asm::jg(label_translate(bb.fail)));
      break;
    case rtl::Bbranch::JG:
    case rtl::Bbranch::JNLE:
      append(Asm::jle(label_translate(bb.fail)));
      break;
    case rtl::Bbranch::JGE:
    case rtl::Bbranch::JNL:
      append(Asm::jl(label_translate(bb.fail)));
      break;
    }
    append(Asm::jmp(label_translate(bb.succ)));
  }

  void visit(rtl::Call const &c) override {
    // TODO: handle more than one argument
    assert(c.args.size() == 1);
    Pseudo arg1 = lookup(c.args[0]);
    append(Asm::movq(arg1, Pseudo{reg::rdi}));
    append(Asm::call(std::string{c.func}));
    Pseudo ret = lookup(c.ret);
    append(Asm::movq(Pseudo{reg::rax}, ret));
    append(Asm::jmp(label_translate(c.succ)));
  }

  void visit(rtl::Return const &ret) override {
    Pseudo arg = lookup(ret.arg);
    append(Asm::movq(arg, Pseudo{reg::rax}));
    append(Asm::jmp(exit_label));
  }

  void visit(rtl::Goto const &go) override {
    append(Asm::jmp(label_translate(go.succ)));
  }
};

AsmProgram rtl_to_asm(rtl::Program const &prog) {
  InstrCompiler icomp{prog.name};
  for (auto const &l : prog.schedule) {
    icomp.append_label(l);
    prog.body.find(l)->second->accept(icomp);
  }
  return icomp.finalize();
}

} // namespace bx
