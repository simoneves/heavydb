/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// With pushConstantSharedNameTest.frag, mirrors PolyMark's capture shaders: each stage
// has its own push constant block, with its own type name, over its own span, but both
// blocks have the same instance name.

layout(std430, binding = 0) uniform SHARED_UBO {
  mat4 viewTM;
};

layout(push_constant) uniform VERT_SHARED_NAME_PUSH_CONSTANTS {
  uint vert_value;
} pushConstants;

in vec3 in_position;

void main() {
  gl_Position = viewTM * vec4(in_position, float(pushConstants.vert_value));
}
