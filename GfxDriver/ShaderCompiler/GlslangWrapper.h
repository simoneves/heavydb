/*
 * SPDX-FileCopyrightText: Copyright (c) 2018-2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <set>
#include <string>
#include <vector>

#include <glslang/Public/ShaderLang.h>

#include "GfxDriver/ShaderCompiler/ShaderCompilerBackend.h"
#include "GfxDriver/ShaderCompiler/Types.h"

namespace gfx {

class GlslangIncluder : public glslang::TShader::Includer {
 public:
  explicit GlslangIncluder(const Library& library);
  ~GlslangIncluder() override;

  // TODO(scb): any use for system vs local (currently both are treated the same)?
  IncludeResult* includeSystem(const char* header_name,
                               const char* includer_name,
                               size_t inclusion_depth) override;
  IncludeResult* includeLocal(const char* header_name,
                              const char* includer_name,
                              size_t inclusion_depth) override;

  void releaseInclude(IncludeResult*) override;

  GlslangIncluder() = delete;

 private:
  using IncludeResultUqPtr = std::unique_ptr<IncludeResult>;
  using IncludeResultSet = std::set<IncludeResultUqPtr>;

  IncludeResultSet result_set_;
  const Library& library_;
};

class GlslangWrapper final : public ShaderCompilerBackend {
 public:
  explicit GlslangWrapper(const Library& library);
  ~GlslangWrapper() override;

  // The stages get a glslang program each, because a material may hold two shaders of
  // the same stage and one program has room for only one intermediate per stage. They
  // do share a single I/O resolver, which is the only thing here that sees the whole
  // material, and so the only place bindings can be assigned consistently across it.
  CompileResults compileMaterial(const std::vector<StageSource>& stages) override;

  GlslangWrapper() = delete;

 private:
  GlslangIncluder includer_;
  TBuiltInResource resources_;

  using TShaderUqPtr = std::unique_ptr<glslang::TShader>;
  using TProgramUqPtr = std::unique_ptr<glslang::TProgram>;
};

}  // namespace gfx
