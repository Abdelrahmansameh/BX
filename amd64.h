#pragma once

/**
 * This module defines the final concrete target of the compilation, which is
 * amd64 assembly.
 */

#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

namespace bx {
namespace amd64 {

// Hardware features: registers and memory addresses

using Label = std::string;

using Reg = char const *;

namespace reg {
#define R(reg)                                                                 \
  reg = Reg { "%" #reg }
constexpr Reg R(rax), R(rbx), R(rcx), R(rdx);
constexpr Reg R(rbp), R(rsi), R(rdi), R(rsp);
constexpr Reg R(r8), R(r9), R(r10), R(r11);
constexpr Reg R(r12), R(r13), R(r14), R(r15);
constexpr Reg R(rip), R(rflags);
#undef R
} // namespace reg

// Abstract assembly features: pseudos

using StackSlot = int32_t;

struct Pseudo {
  const int id;
  // A pseudo can be unbound, or allocated to either a register or a
  // stack slot.
  using binding_t = std::optional<std::variant<Reg, StackSlot>>;
  binding_t binding;
  explicit Pseudo() : id{__last_pseudo_id++}, binding{std::nullopt} {}
  template <typename T>
  explicit Pseudo(T const &reg)
      : id(__last_pseudo_id++), binding{std::variant<Reg, StackSlot>{reg}} {}
  bool operator==(Pseudo const &other) const noexcept { return id == other.id; }

private:
  static int __last_pseudo_id;
};

std::ostream &operator<<(std::ostream &out, Pseudo const &p);

// Assembly

struct Asm {
  /** pseudos that are read */
  std::vector<Pseudo> use;

  /** pseudos that are written to */
  std::vector<Pseudo> def;

  /** labels that are mentioned as arguments */
  std::vector<Label> jump_dests;

  /**
   * The representation template. This string is allowed to contain
   * the following kinds of occurrences which are replaced automatically
   * by the elements of the use/def/jump_dests vectors above.
   *
   *   `s0, `s1, ...     -- source pseudos (use)
   *   `d0, `d1, ...     -- destination pseudos (def)
   *   `j0, `j1, ...     -- jump labels (jump_dests)
   */
  const std::string repr_template;

  using ptr = std::unique_ptr<Asm>;

private:
  /**
   * The constructor is hidden so that only the static methods of this class
   * can be used to build instances
   */
  explicit Asm(std::vector<Pseudo> const &use, std::vector<Pseudo> const &def,
               std::vector<Label> const &dests, std::string const &repr)
      : use{use}, def{def}, jump_dests{dests}, repr_template{repr} {}

public:
  static ptr directive(std::string const &directive) {
    return std::unique_ptr<Asm>(
        new Asm{{}, {}, {}, std::string{"\t"} + directive});
  }

  static ptr set_label(std::string const &label) {
    return std::unique_ptr<Asm>(new Asm{{}, {}, {}, label + ":"});
  }

#define ARITH_BINOP(mnemonic)                                                  \
  static ptr mnemonic##q(int64_t imm, Pseudo const &dest) {                    \
    std::string repr = "\t" #mnemonic "q $" + std::to_string(imm) + ", `d0";   \
    return std::unique_ptr<Asm>(new Asm{{}, {dest}, {}, repr});                \
  }                                                                            \
  static ptr mnemonic##q(Pseudo const &src, Pseudo const &dest) {              \
    return std::unique_ptr<Asm>(                                               \
        new Asm{{src}, {dest}, {}, "\t" #mnemonic "q `s0, `d0"});              \
  }
  ARITH_BINOP(mov)
  ARITH_BINOP(movabs)
  ARITH_BINOP(add)
  ARITH_BINOP(sub)
  ARITH_BINOP(and)
  ARITH_BINOP(or)
  ARITH_BINOP(xor)
#undef ARITH_BINOP

  static ptr cqo() {
    return std::unique_ptr<Asm>(new Asm{
        {Pseudo{reg::rax}}, {Pseudo{reg::rax}, Pseudo{reg::rdx}}, {}, "\tcqo"});
  }

  static ptr imulq(Pseudo const &factor) {
    return std::unique_ptr<Asm>(new Asm{{factor, Pseudo{reg::rax}},
                                        {Pseudo{reg::rax}, Pseudo{reg::rdx}},
                                        {},
                                        "imulq `s0"});
  }

  static ptr idivq(Pseudo const &divisor) {
    return std::unique_ptr<Asm>(
        new Asm{{divisor, Pseudo{reg::rax}, Pseudo{reg::rdx}},
                {Pseudo{reg::rax}, Pseudo{reg::rdx}},
                {},
                "idivq `s0"});
  }

  static ptr cmpq(Pseudo const &arg1, Pseudo const &arg2) {
    return std::unique_ptr<Asm>(
        new Asm{{arg1, arg2}, {}, {}, "\tcmpq `s0, `s1"});
  }

  static ptr cmpq(int32_t imm, Pseudo const &arg) {
    std::string repr = "cmpq $" + std::to_string(imm) + ", `s0";
    return std::unique_ptr<Asm>(new Asm{{arg}, {}, {}, repr});
  }

#define ARITH_UNOP(mnemonic)                                                   \
  static ptr mnemonic##q(Pseudo const &arg) {                                  \
    return std::unique_ptr<Asm>(                                               \
        new Asm{{arg}, {arg}, {}, "\t" #mnemonic "q `s0"});                    \
  }
  ARITH_UNOP(neg)
  ARITH_UNOP(not)
#undef ARITH_UNOP

  static ptr pushq(Pseudo const &arg) {
    return std::unique_ptr<Asm>(new Asm{{arg}, {}, {}, "\tpushq `s0"});
  }

  static ptr popq(Pseudo const &arg) {
    return std::unique_ptr<Asm>(new Asm{{}, {arg}, {}, "\tpopq `d0"});
  }

#define SHIFTOP(mnemonic)                                                      \
  static ptr mnemonic##q(Pseudo const &dest) {                                 \
    return std::unique_ptr<Asm>(                                               \
        new Asm{{Pseudo{reg::rcx}}, {dest}, {}, "\t" #mnemonic "q %cl, `d0"}); \
  }
  SHIFTOP(sar)
  SHIFTOP(shr) // not really used in this course
  SHIFTOP(sal)
#undef SHIFTOP

#define BRANCH_OP(mnemonic)                                                    \
  static ptr mnemonic(Label const &destination) {                              \
    return std::unique_ptr<Asm>(                                               \
        new Asm{{}, {}, {destination}, "\t" #mnemonic " `j0"});                \
  }
  BRANCH_OP(jmp)
  BRANCH_OP(je)
  BRANCH_OP(jne)
  BRANCH_OP(jl)
  BRANCH_OP(jle)
  BRANCH_OP(jg)
  BRANCH_OP(jge)
#undef BRANCH_OP

  static ptr call(Label const &func) {
    return std::unique_ptr<Asm>(
        new Asm{{}, {Pseudo{reg::rax}}, {}, "\tcall " + func});
  }

  static ptr ret() {
    return std::unique_ptr<Asm>(new Asm{{}, {}, {}, "\tret"});
  }
};

std::ostream &operator<<(std::ostream &out, Asm const &line);

} // namespace amd64
} // namespace bx
