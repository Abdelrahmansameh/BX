#pragma once

#include "ast.h"
#include "rtl.h"

namespace bx {
namespace rtl {

std::map<std::string, int> getGlobals(source::Program const &src_prog);
rtl::Program transform(source::Program const &prog);

} // namespace rtl
} // namespace bx