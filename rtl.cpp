#include <iostream>
#include <memory>

#include "rtl.h"

namespace bx {
namespace rtl {

std::ostream &operator<<(std::ostream &out, Label const &l) {
  return out << 'L' << l.id;
}

std::ostream &operator<<(std::ostream &out, Pseudo const &p) {
  if (p.id == discard_pr.id)
    return out << "##";
  return out << '#' << p.id;
}

const std::map<Unop::Code, char const *> Unop::code_map{
    {Unop::Code::NOT, "not"}, {Unop::Code::NEG, "neg"}};

const std::map<Binop::Code, char const *> Binop::code_map{
    {Binop::Code::ADD, "add"}, {Binop::Code::SUB, "sub"},
    {Binop::Code::MUL, "mul"}, {Binop::Code::DIV, "div"},
    {Binop::Code::REM, "rem"}, {Binop::Code::SAL, "sal"},
    {Binop::Code::SAR, "sar"}, {Binop::Code::AND, "and"},
    {Binop::Code::OR, "or"},   {Binop::Code::XOR, "xor"},
};

const std::map<Ubranch::Code, char const *> Ubranch::code_map{
    {Ubranch::Code::JZ, "jz"},
    {Ubranch::Code::JNZ, "jnz"},
};

const std::map<Bbranch::Code, char const *> Bbranch::code_map{
    {Bbranch::Code::JE, "je"},   {Bbranch::Code::JNE, "jne"},
    {Bbranch::Code::JL, "jl"},   {Bbranch::Code::JNL, "jnl"},
    {Bbranch::Code::JLE, "jle"}, {Bbranch::Code::JNLE, "jnle"},
    {Bbranch::Code::JG, "jg"},   {Bbranch::Code::JNG, "jng"},
    {Bbranch::Code::JGE, "jge"}, {Bbranch::Code::JNGE, "jnge"},
};

std::ostream &operator<<(std::ostream &out, Callable const &cbl) {
  out << "CALLABLE \"" << cbl.name << "\":";
  out << "\ninput(s): ";
  for (auto const &reg : cbl.input_regs)
    out << reg << ' ';
  out << "\noutput: " << cbl.output_reg;
  out << "\nenter: " << cbl.enter << "\nleave: " << cbl.leave;
  out << "\n----\n";
  for (auto const &in_lab : cbl.schedule)
    out << in_lab << ": " << *(cbl.body.at(in_lab)) << '\n';
  return out << "END CALLABLE\n\n";
}

} // namespace rtl
} // namespace bx