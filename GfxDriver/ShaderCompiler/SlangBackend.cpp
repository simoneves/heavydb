/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef HAVE_SLANG

#include "GfxDriver/ShaderCompiler/SlangBackend.h"

#include <array>
#include <atomic>
#include <cstring>
#include <string>
#include <string_view>

#include <slang-com-ptr.h>
#include <slang.h>

#include "GfxDriver/RenderError.h"
#include "GfxDriver/ShaderCompiler/Library.h"

namespace gfx {

namespace {

SlangStage to_slang_stage(ShaderStage stage) {
  switch (stage) {
    case ShaderStage::kVertex:
      return SLANG_STAGE_VERTEX;
    case ShaderStage::kFragment:
      return SLANG_STAGE_FRAGMENT;
    case ShaderStage::kGeometry:
      return SLANG_STAGE_GEOMETRY;
    case ShaderStage::kTessControl:
      return SLANG_STAGE_HULL;
    case ShaderStage::kTessEval:
      return SLANG_STAGE_DOMAIN;
    case ShaderStage::kCompute:
      return SLANG_STAGE_COMPUTE;
    case ShaderStage::kRayGen:
      return SLANG_STAGE_RAY_GENERATION;
    case ShaderStage::kAnyHit:
      return SLANG_STAGE_ANY_HIT;
    case ShaderStage::kClosestHit:
      return SLANG_STAGE_CLOSEST_HIT;
    case ShaderStage::kMiss:
      return SLANG_STAGE_MISS;
    case ShaderStage::kIntersection:
      return SLANG_STAGE_INTERSECTION;
    case ShaderStage::kCallable:
      return SLANG_STAGE_CALLABLE;
    case ShaderStage::kMesh:
      return SLANG_STAGE_MESH;
    case ShaderStage::kTask:
      return SLANG_STAGE_AMPLIFICATION;
  }
  return SLANG_STAGE_NONE;
}

std::string blob_to_string(ISlangBlob* blob) {
  if (blob == nullptr) {
    return {};
  }
  return std::string(static_cast<const char*>(blob->getBufferPointer()),
                     blob->getBufferSize());
}

bool same_uuid(const SlangUUID& lhs, const SlangUUID& rhs) {
  return std::memcmp(&lhs, &rhs, sizeof(SlangUUID)) == 0;
}

// Serves #include directives from the Library, as GlslangIncluder does for glslang.
// The assembled source names its includes rather than containing them, so without
// this nearly every shader has undefined functions.
//
// Slang resolves an include relative to the including file before trying it as
// given, so a name can arrive with a directory prefixed to it. Each leading directory
// is dropped in turn until the remainder is a library item.
class LibraryFileSystem final : public ISlangFileSystem {
 public:
  explicit LibraryFileSystem(const Library& library) : library_(library) {}

  SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(SlangUUID const& uuid,
                                                        void** out_object) override {
    if (auto* interface = castAs(uuid)) {
      addRef();
      *out_object = interface;
      return SLANG_OK;
    }
    *out_object = nullptr;
    return SLANG_E_NO_INTERFACE;
  }

  SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return ++ref_count_; }

  SLANG_NO_THROW uint32_t SLANG_MCALL release() override {
    auto const count = --ref_count_;
    if (count == 0) {
      delete this;
    }
    return count;
  }

  SLANG_NO_THROW void* SLANG_MCALL castAs(const SlangUUID& guid) override {
    if (same_uuid(guid, ISlangUnknown::getTypeGuid()) ||
        same_uuid(guid, ISlangCastable::getTypeGuid()) ||
        same_uuid(guid, ISlangFileSystem::getTypeGuid())) {
      return static_cast<ISlangFileSystem*>(this);
    }
    return nullptr;
  }

  SLANG_NO_THROW SlangResult SLANG_MCALL loadFile(char const* path,
                                                  ISlangBlob** out_blob) override {
    std::string_view name(path);
    while (name.substr(0, 2) == "./") {
      name.remove_prefix(2);
    }
    while (!name.empty()) {
      std::string const candidate(name);
      if (library_.contains(candidate)) {
        auto const& code = library_.get(candidate).code;
        *out_blob = slang_createBlob(code.data(), code.size());
        return SLANG_OK;
      }
      auto const slash = name.find('/');
      if (slash == std::string_view::npos) {
        break;
      }
      name.remove_prefix(slash + 1);
    }
    *out_blob = nullptr;
    return SLANG_E_NOT_FOUND;
  }

 private:
  const Library& library_;
  std::atomic<uint32_t> ref_count_{0};
};

}  // namespace

struct SlangBackend::Impl {
  explicit Impl(const Library& library) : file_system(new LibraryFileSystem(library)) {
    // GLSL support is a separate module (libslang-glsl-module) that the global session
    // loads only when asked to. Without it the GLSL built-ins are undefined.
    SlangGlobalSessionDesc desc;
    desc.enableGLSL = true;
    if (SLANG_FAILED(slang::createGlobalSession(&desc, global_session.writeRef()))) {
      THROW_RUNTIME_EX("Failed to create a Slang global session");
    }
    spirv_profile = global_session->findProfile("spirv_1_6");
  }

  Slang::ComPtr<slang::IGlobalSession> global_session;
  Slang::ComPtr<ISlangFileSystem> file_system;
  SlangProfileID spirv_profile = SLANG_PROFILE_UNKNOWN;
};

SlangBackend::SlangBackend(const Library& library)
    : impl_(std::make_unique<Impl>(library)) {}

SlangBackend::~SlangBackend() = default;

ShaderCompilerBackend::CompileResults SlangBackend::compileMaterial(
    const std::vector<StageSource>& stages) {
  CompileResults results;
  results.stages.resize(stages.size());
  if (stages.empty()) {
    return results;
  }

  // As in GlslangWrapper: every failure stops the whole material, so each is recorded
  // against the material as well as against its own stage
  auto const fail_stage = [&results](size_t stage_index, std::string error_string) {
    results.material_error += error_string;
    results.stages[stage_index].second = std::move(error_string);
  };

  std::lock_guard<std::mutex> lock(mutex_);

  // Settings established by PR 0b's spike. Slang names matrix layouts the other way
  // round from GLSL, so its row-major is GLSL's column-major memory layout; with
  // column-major every mat3x2 got the wrong stride. Entry points keep their names,
  // since the pipelines are created with the name the material gives, which is not
  // always main.
  slang::TargetDesc target;
  target.format = SLANG_SPIRV;
  target.profile = impl_->spirv_profile;
  target.forceGLSLScalarBufferLayout = true;

  std::array<slang::CompilerOptionEntry, 2> options{};
  options[0].name = slang::CompilerOptionName::EmitSpirvDirectly;
  options[0].value.intValue0 = 1;
  options[1].name = slang::CompilerOptionName::VulkanUseEntryPointName;
  options[1].value.intValue0 = 1;

  slang::SessionDesc session_desc;
  session_desc.targets = &target;
  session_desc.targetCount = 1;
  session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
  session_desc.fileSystem = impl_->file_system.get();
  session_desc.allowGLSLSyntax = true;
  session_desc.compilerOptionEntries = options.data();
  session_desc.compilerOptionEntryCount = static_cast<uint32_t>(options.size());

  // A session per material, so nothing one material loads is visible to the next
  Slang::ComPtr<slang::ISession> session;
  if (SLANG_FAILED(
          impl_->global_session->createSession(session_desc, session.writeRef()))) {
    fail_stage(0, "Failed to create a Slang session");
    return results;
  }

  for (size_t i = 0; i < stages.size(); ++i) {
    auto const& stage = stages[i];
    auto const describe = [&stage](std::string_view what, ISlangBlob* diagnostics) {
      return std::string(what) + " for shader \"" + stage.pretty_name + "\":\n" +
             blob_to_string(diagnostics);
    };
    // Warnings matter here: narrowing a double literal to float, for one, was only a
    // warning, so log them rather than let a successful compile hide them
    auto const log_warnings = [&stage](ISlangBlob* diagnostics) {
      if (diagnostics != nullptr && diagnostics->getBufferSize() > 0) {
        LOG(WARNING) << "Slang diagnostics for shader \"" << stage.pretty_name << "\":\n"
                     << blob_to_string(diagnostics);
      }
    };

    // The path only labels diagnostics; allowGLSLSyntax is what admits the GLSL
    auto const module_name = "stage" + std::to_string(i);
    Slang::ComPtr<slang::IBlob> diagnostics;
    Slang::ComPtr<slang::IModule> module(
        session->loadModuleFromSourceString(module_name.c_str(),
                                            stage.pretty_name.c_str(),
                                            stage.source.c_str(),
                                            diagnostics.writeRef()));
    if (!module) {
      fail_stage(i, describe("Slang failed to compile", diagnostics));
      return results;
    }
    log_warnings(diagnostics);

    auto const entry_point_name =
        stage.entry_point.empty() ? std::string("main") : stage.entry_point;
    Slang::ComPtr<slang::IEntryPoint> entry_point;
    diagnostics = nullptr;
    if (SLANG_FAILED(module->findAndCheckEntryPoint(entry_point_name.c_str(),
                                                    to_slang_stage(stage.shader_stage),
                                                    entry_point.writeRef(),
                                                    diagnostics.writeRef()))) {
      fail_stage(
          i,
          describe("Slang found no entry point '" + entry_point_name + "'", diagnostics));
      return results;
    }

    std::array<slang::IComponentType*, 2> components{module.get(), entry_point.get()};
    Slang::ComPtr<slang::IComponentType> composite;
    diagnostics = nullptr;
    if (SLANG_FAILED(session->createCompositeComponentType(components.data(),
                                                           components.size(),
                                                           composite.writeRef(),
                                                           diagnostics.writeRef()))) {
      fail_stage(i, describe("Slang failed to compose", diagnostics));
      return results;
    }

    Slang::ComPtr<slang::IComponentType> linked;
    diagnostics = nullptr;
    if (SLANG_FAILED(composite->link(linked.writeRef(), diagnostics.writeRef()))) {
      fail_stage(i, describe("Slang failed to link", diagnostics));
      return results;
    }

    Slang::ComPtr<slang::IBlob> code;
    diagnostics = nullptr;
    if (SLANG_FAILED(
            linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef())) ||
        !code || code->getBufferSize() == 0 ||
        code->getBufferSize() % sizeof(uint32_t) != 0) {
      fail_stage(i, describe("Slang failed to generate SPIR-V", diagnostics));
      return results;
    }
    log_warnings(diagnostics);

    auto& spirv = results.stages[i].first;
    spirv.resize(code->getBufferSize() / sizeof(uint32_t));
    std::memcpy(spirv.data(), code->getBufferPointer(), code->getBufferSize());
  }

  return results;
}

}  // namespace gfx

#endif  // HAVE_SLANG
