/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "GfxDriver/ShaderCompiler/Types.h"

namespace gfx {

// Turns the assembled GLSL of one material into SPIR-V, a stage at a time but in a
// single call, so that whatever the backend assigns across stages (bindings, varying
// locations) is consistent for the whole material.
//
// Everything either side of this is backend-independent: ShaderManager assembles the
// source, including resolving subroutine bindings, before any backend sees it, and
// reflection is read back out of the SPIR-V by ShaderRedecorator afterwards.
class ShaderCompilerBackend {
 public:
  // The SPIR-V, or empty on failure, and the error text for this stage
  using CompileResult = std::pair<spirv_t, std::string>;

  // One stage's worth of input to a material's compile
  struct StageSource {
    std::string pretty_name;
    std::string source;
    std::string entry_point;
    ShaderStage shader_stage;
  };

  struct CompileResults {
    // One per input stage, in the order given
    std::vector<CompileResult> stages;
    // A failure in any one stage stops the whole material, so this carries the reason
    // for the benefit of the stages that had nothing wrong with them
    std::string material_error;
  };

  virtual ~ShaderCompilerBackend() = default;

  // Compiles every stage of one material. A failure is recorded against the stage it
  // belongs to and appended to material_error as well. Any stage left without SPIR-V
  // fails the material, so a caller reporting on a stage with no error of its own
  // should report material_error instead.
  virtual CompileResults compileMaterial(const std::vector<StageSource>& stages) = 0;

  ShaderCompilerBackend(const ShaderCompilerBackend&) = delete;
  ShaderCompilerBackend& operator=(const ShaderCompilerBackend&) = delete;

 protected:
  ShaderCompilerBackend() = default;
};

}  // namespace gfx
