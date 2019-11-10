#include "amd64.h"

namespace bx {
namespace amd64 {

int Pseudo::__last_pseudo_id = 0;

std::ostream &operator<<(std::ostream &out, Pseudo const &p) {
  if (!p.binding.has_value())
    return out << "<pseudo#" << p.id << '>';
  auto b = p.binding.value();
  if (auto reg = std::get_if<0>(&b))
    return out << *reg;
  return out << -8 * std::get<1>(b) << "(%rbp)";
}

std::ostream &operator<<(std::ostream &out, Asm const &line) {
  for (std::size_t i = 0; i < line.repr_template.size(); i++)
    if (line.repr_template[i] == '`')
      switch (line.repr_template[++i]) {
      case '`':
        out << '`';
        break;
      case 's':
        out << line.use[line.repr_template[++i] - '0'];
        break;
      case 'd':
        out << line.def[line.repr_template[++i] - '0'];
        break;
      case 'j':
        out << line.jump_dests[line.repr_template[++i] - '0'];
        break;
      default:
        throw std::runtime_error{"bad repr_template"};
      }
    else
      out << line.repr_template[i];

  return out << '\n';
}

} // namespace amd64
} // namespace bx
