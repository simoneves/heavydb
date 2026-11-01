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
// models, which is why a shader is compiled here only once its manifest opts it in.
//
// A material's stages are compiled together, so their bindings are laid out across the
// whole material. A resource shared between stages has to declare its binding
// explicitly to get the same one in each; see compileMaterial.
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
