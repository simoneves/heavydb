/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "GfxDriver/ShaderCompiler/ShaderCompilerBackend.h"
#include "GfxDriver/ShaderCompiler/Types.h"

namespace gfx {

// Compiles the assembled GLSL through Slang's GLSL compatibility mode, so every shader
// in the library is written in the subset of GLSL that Slang models.
//
// A material's stages are compiled together, so their bindings are laid out across the
// whole material. A resource shared between stages has to declare its binding
// explicitly to get the same one in each; see compileMaterial.
//
// Compiled materials are kept in memory, so a material compiled before is not compiled
// again: a third of the compiles a be-render-tests run makes are repeats. A material is
// identified by everything that decides its output: each stage's source, entry point,
// stage and name. The sources name their includes rather than containing them, which is
// safe because a backend's Library never changes; ShaderManager creates a new backend,
// with an empty cache, when it replaces the Library.
class SlangBackend final : public ShaderCompilerBackend {
 public:
  explicit SlangBackend(const Library& library);
  ~SlangBackend() override;

  CompileResults compileMaterial(const std::vector<StageSource>& stages) override;

  // How many compiles the cache has answered, and how many materials it holds
  size_t cacheHits() const;
  size_t cachedMaterials() const;

  // The most materials the cache holds, dropping the least recently used beyond it
  static constexpr size_t kMaxCachedMaterials = 512;

  SlangBackend() = delete;

 private:
  CompileResults compileUncached(const std::vector<StageSource>& stages);

  // Keeps Slang's headers out of everything that includes this one
  struct Impl;
  std::unique_ptr<Impl> impl_;

  // A Slang global session is not thread-safe, and ShaderManager has no locking of
  // its own, so serialize compiles here rather than rely on how it is called. Guards
  // the cache too.
  mutable std::mutex mutex_;

  // Compiled materials by key, the full key rather than a hash of it so that no two
  // materials can ever share an entry, with the keys in order of use, most recent first
  struct CacheEntry {
    CompileResults results;
    std::list<const std::string*>::iterator use;
  };
  std::unordered_map<std::string, CacheEntry> cache_;
  std::list<const std::string*> cache_use_;
  size_t cache_hits_ = 0;
};

}  // namespace gfx
