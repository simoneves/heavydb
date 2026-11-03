/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "GfxDriver/ShaderCompiler/GlslStructBuilder.h"

#include <algorithm>
#include <sstream>

#include "GfxDriver/Resources/BufferLayout.h"
#include "Logger/Logger.h"

namespace gfx {

namespace {

// How many consecutive locations an interface variable of this type occupies. A 64-bit
// three- or four-component vector takes two, and a matrix one per column.
int locations_occupied(BufferAttrType type) {
  switch (type) {
    case BufferAttrType::kVec3d:
    case BufferAttrType::kVec4d:
    case BufferAttrType::kVec3ui64:
    case BufferAttrType::kVec4ui64:
    case BufferAttrType::kVec3i64:
    case BufferAttrType::kVec4i64:
      return 2;
    case BufferAttrType::kMat3x2f:
    case BufferAttrType::kMat3x2d:
      return 3;
    default:
      return 1;
  }
}

}  // namespace

std::ostream& operator<<(std::ostream& os, const GlslStructBuilder::Qualifier value) {
  using e = GlslStructBuilder::Qualifier;
  switch (value) {
    case e::kFlat:
      os << "flat";
      break;
  }
  return os;
}

GlslStructBuilder::GlslStructBuilder(const std::string& name) : name_{name} {}

void GlslStructBuilder::addMember(const std::string& name,
                                  BufferAttrType type,
                                  const std::vector<Qualifier>& qualifiers,
                                  bool is_array,
                                  int array_size) {
  auto result = member_set_.emplace(name);
  CHECK(result.second) << "Duplicate struct member";
  member_vector_.emplace_back(MemberInfo{name, type, qualifiers, is_array, array_size});
}

void GlslStructBuilder::addMember(const std::string& name,
                                  const BaseTypeGLSL& type,
                                  const std::vector<Qualifier>& qualifiers,
                                  const bool is_array,
                                  int array_size) {
  addMember(name, type.attrType(), qualifiers, is_array, array_size);
}

void GlslStructBuilder::addMembersFromLayout(const BaseBufferLayout& layout) {
  auto num_layout_attrs = layout.numAttributes();
  for (int i = 0; i < num_layout_attrs; ++i) {
    auto const& attr_info = layout[i];
    addMember(attr_info.name, attr_info.type);
  }
}

GlslStructBuilder GlslStructBuilder::clone(const std::string& new_name) {
  GlslStructBuilder new_builder(new_name);
  new_builder.member_set_ = member_set_;
  new_builder.member_vector_ = member_vector_;
  return new_builder;
}

std::string GlslStructBuilder::createStructString() const {
  CHECK(!member_vector_.empty());
  std::stringstream ss;
  ss << name_ << " {";
  for (auto const& info : member_vector_) {
    ss << "\n  ";
    ss << to_string_glsl_decl(info.type) << " " << info.name;
    if (info.is_array) {
      ss << "[" << info.array_size << "]";
    }
    ss << ";";
  }
  ss << "\n}";
  return ss.str();
}

std::string GlslStructBuilder::createInterfaceBlockString(
    bool do_auto_locations,
    std::optional<locations_ref_type> reserved_locations,
    bool is_geometry_shader) const {
  CHECK(!member_vector_.empty());
  std::stringstream ss;
  int location = 0;

  // Where every member occupies one location and none is skipped, one location on the
  // block gives each member the same location a location per member would, and it is
  // the form Slang accepts: Slang rejects a location on a member of an interface block.
  // The qualifier follows the in or out the shader template writes before the block,
  // which GLSL 4.6 allows. Otherwise each member keeps a location of its own.
  bool const locate_block =
      do_auto_locations && !reserved_locations && !is_geometry_shader &&
      std::all_of(member_vector_.begin(), member_vector_.end(), [](auto const& info) {
        return !info.is_array && locations_occupied(info.type) == 1;
      });

  // Geometry shaders do not allow anonymous interface blocks due to
  // the unsized array syntax required, so skip outer bracing
  if (!is_geometry_shader) {
    if (locate_block) {
      ss << "layout (location = 0) ";
    }
    ss << name_ << " {";
  }

  // Write individual member decls
  for (auto const& info : member_vector_) {
    ss << "\n  ";

    // Add location
    if (do_auto_locations && !locate_block) {
      ss << "layout (location = " << location << ") ";
      location++;
      if (reserved_locations) {  // skip reserved locations
        while (reserved_locations.value().get().count(location)) {
          location++;
        }
      }
    }

    // Add qualifiers
    for (auto qualifier : info.qualifiers) {
      ss << qualifier << " ";
    }

    // Geometry shaders require the 'in' prefix for each member
    // as our shader template need to be modified to use the in[N].member_name
    // form
    if (is_geometry_shader) {
      ss << " in ";
    }

    // Add the glsl type
    ss << to_string_glsl_decl(info.type) << " " << info.name;

    CHECK_EQ(is_geometry_shader && info.is_array, false)
        << "Geometry shader inputs cannot be sized arrays";

    if (is_geometry_shader) {
      // Add unsized array decorator for geometry inputs
      ss << "[]";
    } else if (info.is_array) {
      // Or add a sized array if specified
      ss << "[" << info.array_size << "]";
    }
    // end of item declaration
    ss << ";";
  }

  // Geometry inputs are not in a block
  if (!is_geometry_shader) {
    ss << "\n}";
  }
  return ss.str();
}

}  // namespace gfx
