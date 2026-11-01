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
std::vector<ShaderReflection> reflect_slang_material(
    slang::ICompileRequest& request,
    const std::vector<ShaderCompilerBackend::StageSource>& stages);

// Describes how a reflection built from the layout disagrees with one read from the
// same stage's SPIR-V, or returns an empty string if it does not. Every item the SPIR-V
// reports must appear in the layout's reflection, identically; the layout's may have
// more, those being the resources the entry point never reads.
std::string compare_with_spirv_reflection(const ShaderReflection& from_layout,
                                          const ShaderReflection& from_spirv);

}  // namespace gfx

#endif  // HAVE_SLANG
