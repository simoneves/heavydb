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
#include <vector>

#include <slang-com-ptr.h>
#include <slang-deprecated.h>
#include <slang.h>

#include "GfxDriver/RenderError.h"
#include "GfxDriver/ShaderCompiler/Library.h"
#include "GfxDriver/ShaderCompiler/ShaderRedecorator.h"
#include "GfxDriver/ShaderCompiler/SlangReflection.h"

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
    // GLSL support is a builtin module the global session has to be given. Asked for
    // with enableGLSL, Slang looks for a precompiled copy in a separate shared library,
    // libslang-glsl-module-<version>, and failing that compiles it and writes a cache
    // file, slang-glsl-module.bin, next to whichever binary holds Slang; linked
    // statically, that is next to the server's executable. Compiling it here instead
    // needs only the compiler library and writes nothing, at about 0.4 seconds once
    // per backend, against about 0.13 from the precompiled copy.
    SlangGlobalSessionDesc desc;
    if (SLANG_FAILED(slang::createGlobalSession(&desc, global_session.writeRef()))) {
      THROW_RUNTIME_EX("Failed to create a Slang global session");
    }
    if (SLANG_FAILED(
            global_session->compileBuiltinModule(slang::BuiltinModuleName::GLSL, 0))) {
      THROW_RUNTIME_EX("Failed to compile Slang's GLSL module");
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

  //
  // Slang drops any global resource the entry point does not use, where glslang keeps
  // every one declared. PreserveParameters does not change that for GLSL input, set
  // either on the session or on the target. See SlangUnusedResourcesTest.
  //
  // Warning 39001, explicit binding overlap, is disabled: the stages of a material are
  // compiled together and share a resource by declaring the same explicit binding in
  // each, which is exactly what it warns about. An overlap that is not meant is still
  // caught, by ShaderRedecorator's clash check.
  //
  // No optimization. Slang optimizes its SPIR-V with spirv-opt, which it loads at run
  // time from a separate shared library, libslang-glslang-<version>; without that
  // library every compile fails. Unoptimized, it needs no library beyond the compiler,
  // and its output is identical with or without it. The glslang path does not run
  // spirv-opt either.
  std::array<slang::CompilerOptionEntry, 4> options{};
  options[0].name = slang::CompilerOptionName::EmitSpirvDirectly;
  options[0].value.intValue0 = 1;
  options[1].name = slang::CompilerOptionName::VulkanUseEntryPointName;
  options[1].value.intValue0 = 1;
  options[2].name = slang::CompilerOptionName::DisableWarnings;
  options[2].value.kind = slang::CompilerOptionValueKind::String;
  options[2].value.stringValue0 = "39001";
  options[3].name = slang::CompilerOptionName::Optimization;
  options[3].value.intValue0 = SLANG_OPTIMIZATION_LEVEL_NONE;

  slang::SessionDesc session_desc;
  session_desc.targets = &target;
  session_desc.targetCount = 1;
  session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
  session_desc.fileSystem = impl_->file_system.get();
  // A quoted include is looked up relative to the including file, but an angle-bracket
  // one, #include <DriverTests/testUtils.glsl>, only on the search paths. The library's
  // root is the one search path, which the file system above resolves.
  char const* search_paths[] = {"."};
  session_desc.searchPaths = search_paths;
  session_desc.searchPathCount = 1;
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

  // The whole material is one compile request, a GLSL translation unit and an entry
  // point per stage, so that Slang lays out the bindings of every stage together, as
  // glslang's shared resolver does. Distinct resources then get distinct bindings
  // across the stages, and a resource shared between stages gets the same binding in
  // each only if it declares it explicitly; left to Slang, each stage's copy is a
  // separate parameter with a binding of its own. The library binds its shared
  // resources explicitly, and opting a shader in requires its reflection to match
  // glslang's, which a split binding would not.
  //
  // A compile request is what slangc uses, rather than the newer module API
  // (loadModuleFromSourceString). The module API loses GLSL's workgroup size
  // declaration, layout(local_size_x = N) in, and emits LocalSize 1 1 1 instead, so
  // every compute dispatch runs a fraction of its invocations and renders wrong without
  // any error. The compile request keeps it. It is declared in slang-deprecated.h, so
  // revisit this if a Slang upgrade drops it or fixes the module API.
  Slang::ComPtr<slang::ICompileRequest> request;
  if (SLANG_FAILED(session->createCompileRequest(request.writeRef()))) {
    fail_stage(0, "Failed to create a Slang compile request");
    return results;
  }
  std::vector<int> entry_points;
  entry_points.reserve(stages.size());
  for (size_t i = 0; i < stages.size(); ++i) {
    auto const& stage = stages[i];
    auto const translation_unit = request->addTranslationUnit(
        SLANG_SOURCE_LANGUAGE_GLSL, ("stage" + std::to_string(i)).c_str());
    request->addTranslationUnitSourceString(
        translation_unit, stage.pretty_name.c_str(), stage.source.c_str());
    auto const entry_point_name =
        stage.entry_point.empty() ? std::string("main") : stage.entry_point;
    entry_points.push_back(request->addEntryPoint(
        translation_unit, entry_point_name.c_str(), to_slang_stage(stage.shader_stage)));
  }

  auto const compiled = request->compile();
  std::string const diagnostics =
      request->getDiagnosticOutput() ? request->getDiagnosticOutput() : "";
  if (SLANG_FAILED(compiled)) {
    // Slang reports the material as a whole, naming the file each message is about, so
    // attribute the failure to every stage it names, or to the first if it names none
    bool attributed = false;
    for (size_t i = 0; i < stages.size(); ++i) {
      if (diagnostics.find(stages[i].pretty_name) != std::string::npos) {
        fail_stage(i,
                   "Slang failed to compile shader \"" + stages[i].pretty_name + "\":\n" +
                       diagnostics);
        attributed = true;
      }
    }
    if (!attributed) {
      fail_stage(0, "Slang failed to compile:\n" + diagnostics);
    }
    return results;
  }
  // Warnings matter here: narrowing a double literal to float, for one, was only a
  // warning, so log them rather than let a successful compile hide them
  if (!diagnostics.empty()) {
    LOG(WARNING) << "Slang diagnostics for shader \"" << stages.front().pretty_name
                 << "\"" << (stages.size() > 1 ? " and the rest of its material" : "")
                 << ":\n"
                 << diagnostics;
  }

  for (size_t i = 0; i < stages.size(); ++i) {
    Slang::ComPtr<ISlangBlob> code;
    if (SLANG_FAILED(
            request->getEntryPointCodeBlob(entry_points[i], 0, code.writeRef())) ||
        !code || code->getBufferSize() == 0 ||
        code->getBufferSize() % sizeof(uint32_t) != 0) {
      fail_stage(
          i, "Slang generated no SPIR-V for shader \"" + stages[i].pretty_name + "\"");
      return results;
    }
    auto& spirv = results.stages[i].first;
    spirv.resize(code->getBufferSize() / sizeof(uint32_t));
    std::memcpy(spirv.data(), code->getBufferPointer(), code->getBufferSize());
  }

  // Reflect from the program layout, which has every declared resource, where the
  // SPIR-V lacks those the entry point never reads. Each stage is also reflected from
  // its SPIR-V the way glslang's is, and everything found there has to appear in the
  // layout's reflection identically. The layout of a storage block is computed apart
  // from the one Slang emitted, so this is what guarantees the two never disagree
  // about a resource the shader actually uses. Any failure fails the whole material.
  auto const fail_material = [&results, &stages](std::string error) {
    for (auto& stage : results.stages) {
      stage.first.clear();
    }
    results.material_error += error;
    results.stages[0].second = std::move(error);
  };
  try {
    std::vector<ShaderReflection> from_spirv(stages.size());
    for (size_t i = 0; i < stages.size(); ++i) {
      ShaderRedecorator redecorator(stages[i].pretty_name);
      redecorator.redecorate(
          results.stages[i].first, from_spirv[i], stages[i].pretty_name);
    }
    auto reflections = reflect_slang_material(*request, stages, from_spirv);
    for (size_t i = 0; i < stages.size(); ++i) {
      auto const differences =
          compare_with_spirv_reflection(reflections[i], from_spirv[i]);
      if (!differences.empty()) {
        fail_material("Slang's layout and its SPIR-V disagree about shader \"" +
                      stages[i].pretty_name + "\":\n" + differences);
        return results;
      }
    }
    results.reflections = std::move(reflections);
  } catch (std::exception& e) {
    fail_material("Failed to reflect Slang's layout for shader \"" +
                  stages.front().pretty_name + "\": " + e.what());
  }

  return results;
}

}  // namespace gfx

#endif  // HAVE_SLANG
