/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// Prints the reflection the renderer would build from a SPIR-V module, as
// ShaderRedecorator reads it, followed by its execution modes: the same text as a
// .reflect golden. For comparing what two compilers make of one shader, which is what
// compare_compilers.py uses it for.
//
// Usage: SpirvReflect <module.spv>
// Exits 0 having printed the reflection, or 1 with the reason on stderr.

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <spirv-tools/libspirv.hpp>

#include "GfxDriver/ShaderCompiler/ShaderRedecorator.h"
#include "GfxDriver/ShaderCompiler/ShaderReflection.h"

namespace {

// Every OpExecutionMode, less the entry point's id, which differs between compilers
// for nothing
std::string execution_modes(const gfx::spirv_t& spirv) {
  spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_3);
  std::string text;
  if (!tools.Disassemble(spirv, &text, SPV_BINARY_TO_TEXT_OPTION_NONE)) {
    return "(disassembly failed)\n";
  }
  std::istringstream lines(text);
  std::string line, modes;
  while (std::getline(lines, line)) {
    auto const at = line.find("OpExecutionMode");
    if (at == std::string::npos) {
      continue;
    }
    auto mode = line.substr(at);
    auto const id = mode.find(" %");
    if (id != std::string::npos) {
      auto const after_id = mode.find(' ', id + 2);
      mode.erase(id, after_id == std::string::npos ? std::string::npos : after_id - id);
    }
    modes += mode + "\n";
  }
  return modes;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <module.spv>" << std::endl;
    return 1;
  }
  std::ifstream file(argv[1], std::ios::binary);
  if (!file) {
    std::cerr << "Cannot open " << argv[1] << std::endl;
    return 1;
  }
  std::vector<char> const bytes((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
  if (bytes.empty() || bytes.size() % sizeof(uint32_t) != 0) {
    std::cerr << argv[1] << " is not a SPIR-V module" << std::endl;
    return 1;
  }
  gfx::spirv_t spirv(bytes.size() / sizeof(uint32_t));
  std::copy(bytes.begin(), bytes.end(), reinterpret_cast<char*>(spirv.data()));

  try {
    gfx::ShaderRedecorator redecorator(argv[1]);
    gfx::ShaderReflection reflection;
    redecorator.redecorate(spirv, reflection, argv[1]);
    reflection.serialize(std::cout);
    std::cout << execution_modes(spirv);
  } catch (std::exception const& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
  return 0;
}
