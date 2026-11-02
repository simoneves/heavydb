/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "GfxDriver/Resources/Enums.h"
#include "GfxDriver/TypeGLSL.h"

namespace gfx {

class GlslStructBuilder {
 public:
  enum class Qualifier { kFlat };

  explicit GlslStructBuilder(const std::string& name);

  void addMember(const std::string& name,
                 BufferAttrType type,
                 const std::vector<Qualifier>& qualifiers = {},
                 bool is_array = false,
                 int array_size = 1);
  void addMember(const std::string& name,
                 const BaseTypeGLSL& type,
                 const std::vector<Qualifier>& qualifiers = {},
                 bool is_array = false,
                 int array_size = 1);

  // Add struct members for each attribute in a BufferLayout
  void addMembersFromLayout(const BaseBufferLayout& layout);

  // Create a copy of this builder with a unique name
  GlslStructBuilder clone(const std::string& new_name);

  // Create a struct string that is legal in GLSL and C++ (when using glm)
  // Ignores qualifiers
  std::string createStructString() const;

  // Create a string that can serve as a block definition in GLSL
  // Always includes qualifiers
  // do_auto_locations numbers the members' locations from 0, for shader interface
  // blocks (e.g. vertex to fragment). Where every member takes one location and none is
  // reserved, that is a single `layout (location = 0)` on the block, which Slang accepts
  // and a location per member is not; otherwise each member gets `layout (location=N)`.
  // reserved_locations will be skipped when do_auto_locations is enabled
  using locations_ref_type = std::reference_wrapper<const std::set<int>>;
  std::string createInterfaceBlockString(
      bool do_auto_locations,
      std::optional<locations_ref_type> reserved_locations = std::nullopt,
      bool is_geometry_shader = false) const;

  // The same interface as a Slang struct, for the geometry and mesh stages, whose
  // inputs and outputs Slang's GLSL support does not model: they are entry point
  // parameters there, of a struct type. The struct is named as the block is, leads with
  // gl_Position as SV_Position, and gives each member the location
  // createInterfaceBlockString would, with nointerpolation for flat. It is followed by
  // an X-macro, <name>_MEMBERS(X), expanding X(type, member) for each member, which
  // the shader uses to mirror the members as the globals its GLSL body reads and writes.
  // Array members are not supported.
  std::string createSlangStructString(
      bool do_auto_locations,
      std::optional<locations_ref_type> reserved_locations = std::nullopt) const;

 private:
  struct MemberInfo {
    std::string name;
    BufferAttrType type = BufferAttrType::kCOUNT;
    std::vector<Qualifier> qualifiers;
    bool is_array = false;
    int array_size = 1;
  };

  std::string name_;
  std::vector<MemberInfo> member_vector_;  // maintains ordering
  std::set<std::string> member_set_;       // enforces uniqueness
};

}  // namespace gfx
