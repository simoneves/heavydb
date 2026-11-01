/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef HAVE_SLANG

#include <memory>
#include <mutex>

#include "GfxDriver/ShaderCompiler/ShaderCompilerBackend.h"
#include "GfxDriver/ShaderCompiler/Types.h"

namespace gfx {

// Compiles the assembled GLSL through Slang's GLSL compatibility mode. The source is
// exactly what glslang is given, so this only suits shaders Slang's GLSL subset
// models; see .cursor/plans/pr6-shader-compiler-backend.md for which those are.
//
// Each stage is compiled on its own for now. A material whose stages declare
// resources without explicit bindings will get colliding bindings, which the
// redecorator's clash check rejects; settling that is PR 6g.
class SlangBackend final : public ShaderCompilerBackend {
 public:
  explicit SlangBackend(const Library& library);
  ~SlangBackend() override;

  CompileResults compileMaterial(const std::vector<StageSource>& stages) override;

  SlangBackend() = delete;

 private:
  // Keeps Slang's headers out of everything that includes this one
  struct Impl;
  std::unique_ptr<Impl> impl_;

  // A Slang global session is not thread-safe, and ShaderManager has no locking of
  // its own, so serialize compiles here rather than rely on how it is called
  std::mutex mutex_;
};

}  // namespace gfx

#endif  // HAVE_SLANG
