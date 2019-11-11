#pragma once

#include "amd64.h"
#include "rtl.h"

namespace bx {

using AsmProgram = std::vector<std::unique_ptr<amd64::Asm>>;

std::vector<AsmProgram> rtl_to_asm(rtl::Program const &prog);

} // namespace bx