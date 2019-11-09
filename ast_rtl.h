#pragma once

#include "ast.h"
#include "rtl.h"

namespace bx {
namespace rtl {

rtl::Program transform(source::Program const &prog);

} // namespace rtl
} // namespace bx