/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "GfxDriver/ShaderCompiler/SlangReflection.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>

#include <slang-com-ptr.h>
#include <slang-deprecated.h>
#include <slang.h>
#include <spirv_cross/spirv_cross.hpp>

#include "GfxDriver/RenderError.h"

namespace gfx {

namespace {

using slang::ParameterCategory;
using slang::TypeLayoutReflection;
using slang::TypeReflection;
using slang::VariableLayoutReflection;

// Every resource lands in this one set, as ShaderRedecorator requires
constexpr int kDescriptorSet = 0;

// Slang names a block's type SLANG_ParameterGroup_<name>, which is how the declared
// type name of a block with an instance name has to be recovered
std::string block_type_name(const char* name) {
  constexpr std::string_view kPrefix = "SLANG_ParameterGroup_";
  std::string result = name ? name : "";
  if (result.rfind(kPrefix, 0) == 0) {
    result.erase(0, kPrefix.size());
  }
  return result;
}

// A block is named by its type, as SPIRV-Cross names it for glslang. Without an
// instance name the parameter is named after the block; with one, the type name has to
// come from the element type, Slang having named the parameter after the instance.
std::string block_name(VariableLayoutReflection* param, const char* element_type_name) {
  auto const type_name = block_type_name(element_type_name);
  return type_name.empty() ? std::string(param->getName()) : type_name;
}

// A member's declared size, as SPIRV-Cross reports it for glslang. Slang reports an
// array's size without the padding after its last element, where the declared size
// includes it, so an array is its length times its stride. A runtime-sized array is 0.
size_t declared_member_size(TypeLayoutReflection* type) {
  if (type->getKind() == TypeReflection::Kind::Array) {
    return type->getElementCount() *
           type->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
  }
  auto const size = type->getSize();
  return size == std::numeric_limits<size_t>::max() ? 0 : size;
}

// The size SPIRV-Cross reports as a struct's declared size: the end of its last member,
// without the padding to the struct's alignment that Slang includes
size_t declared_struct_size(TypeLayoutReflection* type) {
  size_t end = 0;
  for (unsigned i = 0; i < type->getFieldCount(); ++i) {
    auto* field = type->getFieldByIndex(i);
    end =
        std::max(end, field->getOffset() + declared_member_size(field->getTypeLayout()));
  }
  return end;
}

// The scalar type underlying a scalar, vector or matrix
TypeReflection::ScalarType leaf_scalar_type(TypeReflection* type) {
  // An array is reached only as a struct's child, int pad[3] in a struct member; a
  // member's own array dimensions are taken off before it is validated
  while (type->getKind() == TypeReflection::Kind::Vector ||
         type->getKind() == TypeReflection::Kind::Matrix ||
         type->getKind() == TypeReflection::Kind::Array) {
    type = type->getElementType();
  }
  return type->getScalarType();
}

// The same whitelist ShaderRedecorator applies
void validate_buffer_attr_type(TypeReflection* type, const std::string& name) {
  switch (leaf_scalar_type(type)) {
    case TypeReflection::ScalarType::Int32:
    case TypeReflection::ScalarType::UInt32:
    case TypeReflection::ScalarType::Int64:
    case TypeReflection::ScalarType::UInt64:
    case TypeReflection::ScalarType::Float32:
    case TypeReflection::ScalarType::Float64:
      return;
    default:
      break;
  }
  THROW_RUNTIME_EX("Unsupported type in UBO/SSBO member '" + name + "'");
}

enum class BufferKind { kUniform, kStorage };

// Copies a block's size and members from the stage's SPIR-V reflection, if the SPIR-V
// has the block, and says whether it did. Slang's SPIR-V lays out a GLSL buffer block
// by its declared rules, std430 or std140, but its reflection API lays out the same
// block's element type with scalar alignment, which is wrong wherever the two differ:
// a vec4 after a float, or the stride of an array of a struct ending in padding. A block
// is in the SPIR-V whenever the entry point uses it, and then in full; only a block the
// entry point never touches is left to the layout's computation.
bool copy_buffer_from_spirv(BufferKind kind,
                            const std::string& block_label,
                            int binding,
                            const ShaderReflection& from_spirv,
                            ShaderReflection& reflection) {
  if (kind == BufferKind::kStorage) {
    if (!from_spirv.hasShaderStorageBuffer(block_label) ||
        from_spirv.getShaderStorageBufferBinding(block_label) != binding) {
      return false;
    }
    reflection.addShaderStorageBuffer(
        block_label,
        kDescriptorSet,
        binding,
        from_spirv.getShaderStorageBufferBlockSize(block_label));
    for (auto const name : from_spirv.getAllShaderStorageBufferAttrNames()) {
      auto const& info = from_spirv.getShaderStorageBufferAttrItemInfo(name);
      if (info.binding_or_location == binding) {
        reflection.addShaderStorageBufferAttr(name,
                                              kDescriptorSet,
                                              binding,
                                              info.offset,
                                              info.block_or_array_size,
                                              info.array_length,
                                              info.array_stride);
      }
    }
  } else {
    if (!from_spirv.hasUniformBuffer(block_label) ||
        from_spirv.getUniformBufferBinding(block_label) != binding) {
      return false;
    }
    reflection.addUniformBuffer(block_label,
                                kDescriptorSet,
                                binding,
                                from_spirv.getUniformBufferBlockSize(block_label));
    for (auto const name : from_spirv.getAllUniformBufferAttrNames()) {
      auto const& info = from_spirv.getUniformBufferAttrItemInfo(name);
      if (info.binding_or_location == binding) {
        reflection.addUniformBufferAttr(name,
                                        kDescriptorSet,
                                        binding,
                                        info.offset,
                                        info.block_or_array_size,
                                        info.array_length,
                                        info.array_stride);
      }
    }
  }
  return true;
}

// Reflects one block's members the way ShaderRedecorator does: one level of struct is
// flattened, with UBO children named member.child and SSBO children by their own name,
// and an array of arrays is reduced to its element count and element stride
void reflect_buffer_members(TypeLayoutReflection* block,
                            BufferKind kind,
                            int binding,
                            ShaderReflection& reflection) {
  for (unsigned i = 0; i < block->getFieldCount(); ++i) {
    auto* field = block->getFieldByIndex(i);
    std::string const member_name = field->getName();
    auto const member_offset = static_cast<int>(field->getOffset());
    auto* member_type = field->getTypeLayout();
    auto const member_size = static_cast<int>(declared_member_size(member_type));

    int array_length = -1;
    int array_stride = -1;
    if (member_type->getKind() == TypeReflection::Kind::Array) {
      auto const outer_length = member_type->getElementCount();
      auto const outer_stride =
          member_type->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
      size_t element_count = outer_length;
      auto* element = member_type->getElementTypeLayout();
      size_t dimensions = 1;
      while (element->getKind() == TypeReflection::Kind::Array) {
        element_count *= element->getElementCount();
        element = element->getElementTypeLayout();
        ++dimensions;
      }
      if (outer_length == 0) {
        // Runtime-sized. Only the outermost dimension may be, and with no element count
        // to divide by, the outer stride is already the element stride.
        RUNTIME_EX_ASSERT(dimensions == 1,
                          "Runtime-sized array member '" + member_name +
                              "' also has inner dimensions, whose extent is lost by "
                              "flattening");
        array_length = 0;
        array_stride = static_cast<int>(outer_stride);
      } else {
        auto const total_bytes = outer_length * outer_stride;
        RUNTIME_EX_ASSERT(total_bytes % element_count == 0,
                          "Array member '" + member_name +
                              "' is padded between dimensions and cannot be flattened");
        array_length = static_cast<int>(element_count);
        array_stride = static_cast<int>(total_bytes / element_count);
      }
      member_type = element;
    }

    auto const add = [&](const std::string& name, int offset, int size) {
      if (kind == BufferKind::kStorage) {
        reflection.addShaderStorageBufferAttr(
            name, kDescriptorSet, binding, offset, size, array_length, array_stride);
      } else {
        reflection.addUniformBufferAttr(
            name, kDescriptorSet, binding, offset, size, array_length, array_stride);
      }
    };

    if (member_type->getKind() == TypeReflection::Kind::Struct) {
      for (unsigned j = 0; j < member_type->getFieldCount(); ++j) {
        auto* child = member_type->getFieldByIndex(j);
        auto* child_type = child->getTypeLayout();
        RUNTIME_EX_ASSERT(child_type->getKind() != TypeReflection::Kind::Struct,
                          "Nested structs not supported in UBO/SSBO");
        validate_buffer_attr_type(child_type->getType(), child->getName());
        std::string child_name = child->getName();
        if (kind == BufferKind::kUniform) {
          child_name = member_name + "." + child_name;
        }
        add(child_name,
            member_offset + static_cast<int>(child->getOffset()),
            static_cast<int>(declared_member_size(child_type)));
      }
    } else {
      validate_buffer_attr_type(member_type->getType(), member_name);
      add(member_name, member_offset, member_size);
    }
  }
}

// The resource kinds ShaderRedecorator distinguishes, for the clash check and its message
enum class ResourceKind {
  kUniformBuffer,
  kShaderStorageBuffer,
  kSampledImage,
  kStorageImage,
  kAccelerationStructure
};

std::string resource_kind_to_string(ResourceKind kind) {
  switch (kind) {
    case ResourceKind::kUniformBuffer:
      return "Uniform Buffer";
    case ResourceKind::kShaderStorageBuffer:
      return "Shader Storage Buffer";
    case ResourceKind::kSampledImage:
      return "Sampled Image";
    case ResourceKind::kStorageImage:
      return "Storage Image";
    case ResourceKind::kAccelerationStructure:
      return "Acceleration Structure";
  }
  return "Resource";
}

// Claims bindings across the whole material, rejecting two different resources on one
// binding. The same resource declared in two stages, which is how a shared resource
// looks, claims the same binding legitimately.
class BindingClaims {
 public:
  explicit BindingClaims(std::string material_name)
      : material_name_(std::move(material_name)) {}

  void claim(int binding, ResourceKind kind, const std::string& name) {
    // A resource declared in more than one stage gets a binding per stage from Slang,
    // which sees a separate parameter in each, unless it declares its binding
    // explicitly
    auto const [named, first_time] = bindings_by_name_.try_emplace({kind, name}, binding);
    if (!first_time && named->second != binding) {
      THROW_RUNTIME_EX(resource_kind_to_string(kind) + " '" + name +
                       "' is declared in more than one stage of '" + material_name_ +
                       "' without an explicit binding, so Slang gives each stage a "
                       "binding of its own (" +
                       std::to_string(named->second) + " and " + std::to_string(binding) +
                       "). Declare it with layout(binding = N).");
    }

    auto const entry = std::make_tuple(kind, name);
    auto const [itr, inserted] = claims_.try_emplace(binding, entry);
    if (!inserted && itr->second != entry) {
      THROW_RUNTIME_EX("Binding clash at " + std::to_string(binding) + ", Set " +
                       std::to_string(kDescriptorSet) + ", Shader '" + material_name_ +
                       "', " + resource_kind_to_string(kind) + " '" + name +
                       "' clashes with " +
                       resource_kind_to_string(std::get<0>(itr->second)) + " '" +
                       std::get<1>(itr->second) + "'. Next available binding is " +
                       std::to_string(claims_.rbegin()->first + 1));
    }
  }

 private:
  std::string material_name_;
  std::map<int, std::tuple<ResourceKind, std::string>> claims_;
  std::map<std::tuple<ResourceKind, std::string>, int> bindings_by_name_;
};

void reflect_parameter(VariableLayoutReflection* param,
                       ShaderStage stage,
                       slang::ProgramLayout* program,
                       BindingClaims& claims,
                       const ShaderReflection& from_spirv,
                       ShaderReflection& reflection) {
  auto const category = param->getCategory();
  auto* type = param->getTypeLayout();
  std::string const name = param->getName();

  // Varyings: only vertex inputs and fragment outputs are reflected, as ShaderRedecorator
  // reflects them
  if (category == ParameterCategory::VaryingInput ||
      category == ParameterCategory::VaryingOutput) {
    auto const location = static_cast<int>(param->getBindingIndex());
    auto const count = type->getKind() == TypeReflection::Kind::Array
                           ? static_cast<int>(type->getElementCount())
                           : 1;
    if (category == ParameterCategory::VaryingInput && stage == ShaderStage::kVertex) {
      reflection.addVertexAttr(name, location, count);
    } else if (category == ParameterCategory::VaryingOutput &&
               stage == ShaderStage::kFragment) {
      for (int i = 0; i < count; ++i) {
        reflection.addFragmentShaderOutputLocation(location + i);
      }
    }
    return;
  }

  if (category == ParameterCategory::PushConstantBuffer) {
    auto* block = type->getElementTypeLayout();
    RUNTIME_EX_ASSERT(block && block->getFieldCount() > 0,
                      "Push constant block '" + name + "' has no members");
    // The block's own span rather than its declared size, as ShaderRedecorator records
    size_t begin = std::numeric_limits<size_t>::max();
    size_t end = 0;
    for (unsigned i = 0; i < block->getFieldCount(); ++i) {
      auto* field = block->getFieldByIndex(i);
      auto const size = declared_member_size(field->getTypeLayout());
      // Slang's SPIR-V honours a member's [[vk::offset]], which is how a stage places
      // its push constants after another stage's range, but its program layout ignores
      // it and packs the member from 0. So the offset comes from the SPIR-V whenever
      // the SPIR-V has the member, which it does whenever the entry point reads it;
      // a member it never reads is never pushed to through this stage either.
      auto const& from_spirv_member =
          from_spirv.getPushConstantItemInfo(field->getName());
      auto const offset = from_spirv_member.offset >= 0
                              ? static_cast<size_t>(from_spirv_member.offset)
                              : field->getOffset();
      reflection.addPushConstantAttr(
          field->getName(), static_cast<int>(offset), static_cast<int>(size));
      begin = std::min(begin, offset);
      end = std::max(end, offset + size);
    }
    // Both the instance name and the type name, as ShaderRedecorator records them
    auto const span = static_cast<int>(end - begin);
    reflection.addPushConstant(name, static_cast<int>(begin), span);
    auto const type_name = block_type_name(block->getName());
    if (!type_name.empty() && type_name != name) {
      reflection.addPushConstant(type_name, static_cast<int>(begin), span);
    }
    return;
  }

  // A specialization constant (layout(constant_id)) is set at pipeline creation by its
  // id, which the caller knows. ShaderRedecorator does not reflect one either.
  if (category == ParameterCategory::SpecializationConstant) {
    return;
  }

  RUNTIME_EX_ASSERT(category == ParameterCategory::DescriptorTableSlot,
                    "Unsupported Slang parameter category " +
                        std::to_string(static_cast<int>(category)) + " for '" + name +
                        "'");
  RUNTIME_EX_ASSERT(static_cast<int>(param->getBindingSpace()) == kDescriptorSet,
                    "'" + name + "' is in descriptor set " +
                        std::to_string(param->getBindingSpace()) +
                        ", which is not supported");
  auto const binding = static_cast<int>(param->getBindingIndex());

  // An array of resources is one binding with a descriptor count
  int count = 1;
  if (type->getKind() == TypeReflection::Kind::Array) {
    count = static_cast<int>(type->getElementCount());
    type = type->getElementTypeLayout();
  }

  switch (type->getKind()) {
    case TypeReflection::Kind::ConstantBuffer: {
      auto* block = type->getElementTypeLayout();
      auto const block_label = block_name(param, block->getName());
      claims.claim(binding, ResourceKind::kUniformBuffer, block_label);
      if (copy_buffer_from_spirv(
              BufferKind::kUniform, block_label, binding, from_spirv, reflection)) {
        return;
      }
      reflection.addUniformBuffer(block_label,
                                  kDescriptorSet,
                                  binding,
                                  static_cast<int>(declared_struct_size(block)));
      reflect_buffer_members(block, BufferKind::kUniform, binding, reflection);
      return;
    }
    case TypeReflection::Kind::ShaderStorageBuffer: {
      // Slang reflects a GLSL buffer block as GLSLShaderStorageBuffer<T, Layout> with no
      // element layout of its own, so lay out T itself
      auto* generic = type->getType()->getGenericContainer();
      RUNTIME_EX_ASSERT(generic && generic->getTypeParameterCount() > 0,
                        "Cannot find the element type of storage block '" + name + "'");
      auto* element = generic->getConcreteType(generic->getTypeParameter(0));
      auto* block = program->getTypeLayout(element, slang::LayoutRules::Default);
      RUNTIME_EX_ASSERT(block, "Cannot lay out storage block '" + name + "'");
      auto const block_label = block_name(param, element->getName());
      claims.claim(binding, ResourceKind::kShaderStorageBuffer, block_label);
      if (copy_buffer_from_spirv(
              BufferKind::kStorage, block_label, binding, from_spirv, reflection)) {
        return;
      }
      reflection.addShaderStorageBuffer(block_label,
                                        kDescriptorSet,
                                        binding,
                                        static_cast<int>(declared_struct_size(block)));
      reflect_buffer_members(block, BufferKind::kStorage, binding, reflection);
      return;
    }
    case TypeReflection::Kind::Resource: {
      auto* resource = type->getType();
      auto const shape = resource->getResourceShape();
      if ((shape & SLANG_RESOURCE_BASE_SHAPE_MASK) == SLANG_ACCELERATION_STRUCTURE) {
        claims.claim(binding, ResourceKind::kAccelerationStructure, name);
        reflection.addAccelerationStructure(name, kDescriptorSet, binding, count);
      } else if (resource->getResourceAccess() == SLANG_RESOURCE_ACCESS_READ_WRITE ||
                 resource->getResourceAccess() == SLANG_RESOURCE_ACCESS_RASTER_ORDERED) {
        // A storage image, readonly or not: Slang reports both as read-write. A
        // rasterizer-ordered one, Slang's form of an image under a pixel interlock, is
        // a storage image too.
        claims.claim(binding, ResourceKind::kStorageImage, name);
        reflection.addStorageImage(name, kDescriptorSet, binding, count);
      } else {
        RUNTIME_EX_ASSERT(shape & SLANG_TEXTURE_COMBINED_FLAG,
                          "'" + name + "' is a separate image, which is not supported");
        claims.claim(binding, ResourceKind::kSampledImage, name);
        reflection.addSampler(name, kDescriptorSet, binding, count);
      }
      return;
    }
    default:
      THROW_RUNTIME_EX("Unsupported Slang resource kind " +
                       std::to_string(static_cast<int>(type->getKind())) + " for '" +
                       name + "'");
  }
}

// One of the GLSL module's own built-in variables. Slang lists them in the program
// layout, as though the shader had declared them, when its GLSL module was compiled
// from source rather than loaded precompiled, which is how the backend gets it. They
// are all varyings and all carry names GLSL reserves, gl_, or _gl_, __gl_ and __sv_ for
// Slang's own.
bool is_glsl_builtin(VariableLayoutReflection* param) {
  auto const category = param->getCategory();
  if (category != ParameterCategory::VaryingInput &&
      category != ParameterCategory::VaryingOutput) {
    return false;
  }
  std::string_view const name = param->getName();
  for (std::string_view prefix : {"gl_", "_gl_", "__gl_", "__sv_"}) {
    if (name.substr(0, prefix.size()) == prefix) {
      return true;
    }
  }
  return false;
}

// A reflection's serialized items, grouped by the section they belong to
std::map<std::string, std::set<std::string>> serialized_items(
    const ShaderReflection& reflection) {
  std::ostringstream stream;
  reflection.serialize(stream);
  std::istringstream lines(stream.str());
  std::map<std::string, std::set<std::string>> items;
  std::string line, section;
  while (std::getline(lines, line)) {
    if (line.rfind("  ", 0) == 0) {
      items[section].insert(line);
    } else {
      section = line.substr(0, line.find(' '));
      items[section];
    }
  }
  return items;
}

}  // namespace

std::vector<ShaderReflection> reflect_slang_material(
    slang::ICompileRequest& request,
    const std::vector<ShaderCompilerBackend::StageSource>& stages,
    const std::vector<ShaderReflection>& from_spirv) {
  CHECK_EQ(from_spirv.size(), stages.size());
  auto* program = reinterpret_cast<slang::ProgramLayout*>(request.getReflection());
  RUNTIME_EX_ASSERT(program, "Slang produced no program layout");

  std::string material_name;
  for (auto const& stage : stages) {
    material_name += (material_name.empty() ? "" : "+") + stage.pretty_name;
  }
  BindingClaims claims(material_name);

  // The program layout lists each translation unit's parameters in turn, among the GLSL
  // module's built-ins, which are skipped, and each unit
  // was given one stage, so its own module layout says how many of them are its own.
  // The module layout's bindings are its own, not the program's, so only its names and
  // order are used, to check the attribution.
  std::vector<ShaderReflection> reflections(stages.size());
  unsigned next = 0;
  for (size_t i = 0; i < stages.size(); ++i) {
    Slang::ComPtr<slang::IModule> module;
    RUNTIME_EX_ASSERT(
        SLANG_SUCCEEDED(request.getModule(static_cast<SlangInt>(i), module.writeRef())) &&
            module,
        "Slang produced no module for shader \"" + stages[i].pretty_name + "\"");
    auto* module_layout = module->getLayout();
    RUNTIME_EX_ASSERT(
        module_layout,
        "Slang produced no layout for shader \"" + stages[i].pretty_name + "\"");

    reflections[i].initialize();
    for (unsigned j = 0; j < module_layout->getParameterCount(); ++j, ++next) {
      while (next < program->getParameterCount() &&
             is_glsl_builtin(program->getParameterByIndex(next))) {
        ++next;
      }
      RUNTIME_EX_ASSERT(next < program->getParameterCount(),
                        "Slang's program layout has fewer parameters than its modules");
      auto* param = program->getParameterByIndex(next);
      std::string const expected = module_layout->getParameterByIndex(j)->getName();
      RUNTIME_EX_ASSERT(expected == param->getName(),
                        "Slang's program layout lists '" + std::string(param->getName()) +
                            "' where shader \"" + stages[i].pretty_name +
                            "\" declares '" + expected + "'");
      reflect_parameter(
          param, stages[i].shader_stage, program, claims, from_spirv[i], reflections[i]);
    }
  }
  while (next < program->getParameterCount() &&
         is_glsl_builtin(program->getParameterByIndex(next))) {
    ++next;
  }
  RUNTIME_EX_ASSERT(next == program->getParameterCount(),
                    "Slang's program layout has parameters no module declares");
  return reflections;
}

std::string compare_with_spirv_reflection(const ShaderReflection& from_layout,
                                          const ShaderReflection& from_spirv) {
  auto const layout_items = serialized_items(from_layout);
  std::string differences;
  for (auto const& [section, items] : serialized_items(from_spirv)) {
    auto const found = layout_items.find(section);
    std::string missing;
    for (auto const& item : items) {
      if (found == layout_items.end() || found->second.count(item) == 0) {
        missing += "  SPIR-V " + section + ":" + item + "\n";
      }
    }
    if (!missing.empty()) {
      // What the layout has in the same section, so the two can be told apart
      differences += missing;
      if (found != layout_items.end()) {
        for (auto const& item : found->second) {
          if (items.count(item) == 0) {
            differences += "  layout " + section + ":" + item + "\n";
          }
        }
      }
    }
  }
  return differences;
}

namespace {

// The order of the stages that pass varyings down the graphics pipeline. A stage's
// inputs come from the nearest stage before it that the material has. The task stage
// is left out: it passes the mesh stage a payload, not varyings.
int pipeline_rank(ShaderStage stage) {
  switch (stage) {
    case ShaderStage::kVertex:
    case ShaderStage::kMesh:
      return 0;
    case ShaderStage::kTessControl:
      return 1;
    case ShaderStage::kTessEval:
      return 2;
    case ShaderStage::kGeometry:
      return 3;
    case ShaderStage::kFragment:
      return 4;
    default:
      return -1;
  }
}

// Whether a stage's inputs or outputs are arrays with an element per vertex, whose outer
// dimension is the pipeline's to supply rather than part of what the stages pass
bool is_per_vertex_array(ShaderStage stage, bool is_input) {
  if (is_input) {
    return stage == ShaderStage::kGeometry || stage == ShaderStage::kTessControl ||
           stage == ShaderStage::kTessEval;
  }
  return stage == ShaderStage::kTessControl || stage == ShaderStage::kMesh;
}

// A scalar type's GLSL name, and the prefix GLSL gives a vector or matrix of it
std::pair<std::string, std::string> base_type_names(const spirv_cross::SPIRType& type) {
  using BaseType = spirv_cross::SPIRType::BaseType;
  switch (type.basetype) {
    case BaseType::Boolean:
      return {"bool", "b"};
    case BaseType::Int:
      return {"int", "i"};
    case BaseType::UInt:
      return {"uint", "u"};
    case BaseType::Int64:
      return {"int64_t", "i64"};
    case BaseType::UInt64:
      return {"uint64_t", "u64"};
    case BaseType::Half:
      return {"float16_t", "f16"};
    case BaseType::Float:
      return {"float", ""};
    case BaseType::Double:
      return {"double", "d"};
    default: {
      auto const name = "type" + std::to_string(static_cast<int>(type.basetype));
      return {name, name};
    }
  }
}

// A type as GLSL would write it, less `drop_outer` of its outermost array dimensions
std::string varying_type_name(const spirv_cross::Compiler& compiler,
                              const spirv_cross::SPIRType& type,
                              size_t drop_outer = 0) {
  std::string name;
  if (type.basetype == spirv_cross::SPIRType::BaseType::Struct) {
    name = "struct {";
    for (size_t i = 0; i < type.member_types.size(); ++i) {
      name += (i ? ", " : " ") +
              varying_type_name(compiler, compiler.get_type(type.member_types[i]));
    }
    name += " }";
  } else {
    auto const [scalar, prefix] = base_type_names(type);
    if (type.columns > 1) {
      name = prefix + "mat" + std::to_string(type.columns) + "x" +
             std::to_string(type.vecsize);
    } else if (type.vecsize > 1) {
      name = prefix + "vec" + std::to_string(type.vecsize);
    } else {
      name = scalar;
    }
  }
  // array.back() is the outermost dimension
  for (size_t i = type.array.size() - std::min(drop_outer, type.array.size()); i-- > 0;) {
    name += "[" + std::to_string(type.array[i]) + "]";
  }
  return name;
}

// The number of locations a type takes, less `drop_outer` of its outermost dimensions
uint32_t varying_location_count(const spirv_cross::Compiler& compiler,
                                const spirv_cross::SPIRType& type,
                                size_t drop_outer = 0) {
  uint32_t count = 0;
  if (type.basetype == spirv_cross::SPIRType::BaseType::Struct) {
    for (auto const member : type.member_types) {
      count += varying_location_count(compiler, compiler.get_type(member));
    }
  } else {
    // A 64-bit vector of three or four components takes two locations
    count = (type.width == 64 && type.vecsize > 2 ? 2 : 1) * std::max(type.columns, 1u);
  }
  for (size_t i = drop_outer; i < type.array.size(); ++i) {
    count *= type.array[type.array.size() - 1 - i];
  }
  return count;
}

struct Varying {
  std::string name;
  std::string type_name;
  const spirv_cross::SPIRType* type;  // A struct member's own, or the variable's
  size_t drop_outer;
};

// One stage's varyings in one direction, by location. A block contributes each of its
// members at the member's own location, or at the next one after the member before.
std::map<uint32_t, Varying> stage_varyings(const spirv_cross::Compiler& compiler,
                                           ShaderStage stage,
                                           bool is_input,
                                           const std::string& stage_name) {
  auto const resources = compiler.get_shader_resources();
  auto const& variables = is_input ? resources.stage_inputs : resources.stage_outputs;
  size_t const drop_outer = is_per_vertex_array(stage, is_input) ? 1 : 0;
  std::map<uint32_t, Varying> varyings;
  auto const add = [&](uint32_t location,
                       std::string name,
                       const spirv_cross::SPIRType& type,
                       size_t drop) {
    // Slang names an entry point's varyings entryPointParam_<entry>.<name>, a prefix
    // that is no help to anyone reading the source
    if (name.rfind("entryPointParam_", 0) == 0 && name.find('.') != std::string::npos) {
      name.erase(0, name.find('.') + 1);
    }
    auto const [itr, inserted] = varyings.try_emplace(
        location, Varying{name, varying_type_name(compiler, type, drop), &type, drop});
    RUNTIME_EX_ASSERT(inserted,
                      "Shader \"" + stage_name + "\" has two " +
                          (is_input ? "inputs" : "outputs") + " at location " +
                          std::to_string(location) + ", '" + itr->second.name +
                          "' and '" + name + "'");
  };

  for (auto const& variable : variables) {
    auto const& type = compiler.get_type(variable.type_id);
    bool const has_location =
        compiler.has_decoration(variable.id, spv::DecorationLocation);
    if (type.basetype != spirv_cross::SPIRType::BaseType::Struct ||
        !compiler.has_decoration(type.self, spv::DecorationBlock)) {
      RUNTIME_EX_ASSERT(has_location,
                        "Shader \"" + stage_name + "\" has " +
                            (is_input ? "input" : "output") + " '" + variable.name +
                            "' without a location");
      add(compiler.get_decoration(variable.id, spv::DecorationLocation),
          variable.name,
          type,
          drop_outer);
      continue;
    }
    // A block, perhaps per vertex. Its members are reported without that dimension,
    // which the block's array carries rather than they.
    uint32_t next =
        has_location ? compiler.get_decoration(variable.id, spv::DecorationLocation) : 0;
    for (uint32_t i = 0; i < type.member_types.size(); ++i) {
      auto const& member_type = compiler.get_type(type.member_types[i]);
      if (compiler.has_member_decoration(type.self, i, spv::DecorationLocation)) {
        next = compiler.get_member_decoration(type.self, i, spv::DecorationLocation);
      } else {
        RUNTIME_EX_ASSERT(has_location,
                          "Shader \"" + stage_name + "\" has block '" + variable.name +
                              "' whose member " + std::to_string(i) + " has no location");
      }
      add(next,
          variable.name + "." + compiler.get_member_name(type.self, i),
          member_type,
          0);
      next += varying_location_count(compiler, member_type);
    }
  }
  return varyings;
}

// An output may have more vector components than the input it feeds, which ignores the
// rest. Anything else must be the same type.
bool varying_types_match(const Varying& output, const Varying& input) {
  if (output.type_name == input.type_name) {
    return true;
  }
  auto const& out_type = *output.type;
  auto const& in_type = *input.type;
  return out_type.basetype != spirv_cross::SPIRType::BaseType::Struct &&
         out_type.basetype == in_type.basetype && out_type.width == in_type.width &&
         out_type.columns == 1 && in_type.columns == 1 &&
         out_type.array.size() == output.drop_outer &&
         in_type.array.size() == input.drop_outer && out_type.vecsize > in_type.vecsize;
}

}  // namespace

std::string check_varying_interfaces(
    const std::vector<ShaderCompilerBackend::StageSource>& stages,
    const std::vector<ShaderCompilerBackend::CompileResult>& compiled) {
  CHECK_EQ(compiled.size(), stages.size());
  std::vector<size_t> order;
  for (size_t i = 0; i < stages.size(); ++i) {
    if (pipeline_rank(stages[i].shader_stage) >= 0) {
      order.push_back(i);
    }
  }
  std::stable_sort(order.begin(), order.end(), [&stages](size_t a, size_t b) {
    return pipeline_rank(stages[a].shader_stage) < pipeline_rank(stages[b].shader_stage);
  });

  std::string errors;
  auto const describe = [](uint32_t location, const Varying& varying) {
    return varying.type_name + " '" + varying.name + "' at location " +
           std::to_string(location);
  };
  for (size_t k = 1; k < order.size(); ++k) {
    auto const& producer = stages[order[k - 1]];
    auto const& consumer = stages[order[k]];
    RUNTIME_EX_ASSERT(
        pipeline_rank(producer.shader_stage) < pipeline_rank(consumer.shader_stage),
        "Shaders \"" + producer.pretty_name + "\" and \"" + consumer.pretty_name +
            "\" are the same pipeline stage");
    spirv_cross::Compiler producer_compiler(compiled[order[k - 1]].first);
    spirv_cross::Compiler consumer_compiler(compiled[order[k]].first);
    auto const outputs = stage_varyings(
        producer_compiler, producer.shader_stage, false, producer.pretty_name);
    auto const inputs = stage_varyings(
        consumer_compiler, consumer.shader_stage, true, consumer.pretty_name);
    std::string pair_errors;
    for (auto const& [location, input] : inputs) {
      auto const output = outputs.find(location);
      if (output == outputs.end()) {
        pair_errors += "  \"" + consumer.pretty_name + "\" reads " +
                       describe(location, input) + ", which \"" + producer.pretty_name +
                       "\" does not write\n";
      } else if (!varying_types_match(output->second, input)) {
        pair_errors += "  \"" + consumer.pretty_name + "\" reads " +
                       describe(location, input) + ", where \"" + producer.pretty_name +
                       "\" writes " + describe(location, output->second) + "\n";
      }
    }
    if (!pair_errors.empty()) {
      // Everything the earlier stage writes, since the locations a mismatch is reported
      // at are the compiler's choice rather than anything in the source
      errors += pair_errors + "  \"" + producer.pretty_name + "\" writes:\n";
      for (auto const& [location, output] : outputs) {
        errors += "    " + describe(location, output) + "\n";
      }
    }
  }
  return errors;
}

}  // namespace gfx
