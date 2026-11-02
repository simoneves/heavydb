/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef HAVE_SLANG

#include <string>
#include <vector>

#include "GfxDriver/ShaderCompiler/ShaderCompilerBackend.h"
#include "GfxDriver/ShaderCompiler/ShaderReflection.h"

namespace slang {
struct ICompileRequest;
}

namespace gfx {

// Builds each stage's ShaderReflection from the program layout of a material Slang has
// just compiled, rather than from its SPIR-V.
//
// The difference that matters: Slang leaves out of the SPIR-V any resource the entry
// point never reads, where glslang keeps every one declared, so a reflection read back
// from Slang's SPIR-V lacks resources the material still binds by name. The program
// layout has every declared resource, with the binding Slang reserved for it.
//
// Produces what ShaderRedecorator produces from glslang's SPIR-V, conventions included:
// a block named by its type, dotted member names in uniform blocks and bare ones in
// storage blocks, arrays of arrays flattened to element count and stride, a push
// constant block's span and both of its names, and the scalar type whitelist. Throws
// on anything it cannot represent, and on a binding clash anywhere in the material.
//
// `from_spirv` is each stage's reflection read from its SPIR-V. The layout decides
// which resources there are and their bindings, but two things are taken from the
// SPIR-V wherever it has them, because Slang's reflection API gets them wrong: a buffer
// block's size and member layout, which it computes with scalar alignment whatever the
// block declares, and push constant member offsets, which ignore [[vk::offset]].
std::vector<ShaderReflection> reflect_slang_material(
    slang::ICompileRequest& request,
    const std::vector<ShaderCompilerBackend::StageSource>& stages,
    const std::vector<ShaderReflection>& from_spirv);

// Describes how a reflection built from the layout disagrees with one read from the
// same stage's SPIR-V, or returns an empty string if it does not. Every item the SPIR-V
// reports must appear in the layout's reflection, identically; the layout's may have
// more, those being the resources the entry point never reads.
std::string compare_with_spirv_reflection(const ShaderReflection& from_layout,
                                          const ShaderReflection& from_spirv);

// Checks that every varying a stage reads is written by the stage before it, at the same
// location and with the same type, and describes each that is not, or returns an empty
// string. Slang compiles each stage on its own and locates any varying without an
// explicit location by declaration order, so two stages that declare theirs in a
// different order would otherwise disagree without a word, the renderer simply reading
// the wrong varying. Built-ins are left out, and so is the per-vertex array of a
// geometry or tessellation input and of a mesh output. Stages that pass no varyings,
// compute, ray tracing and task, are not checked.
std::string check_varying_interfaces(
    const std::vector<ShaderCompilerBackend::StageSource>& stages,
    const std::vector<ShaderCompilerBackend::CompileResult>& compiled);

}  // namespace gfx

#endif  // HAVE_SLANG
