/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// SlangSelectionTest: opted in to the Slang backend in the manifest, unlike its
// fragment shader, so it is compiled by Slang alone and by glslang alongside it.

layout(std430, binding = 0) uniform SELECTION_UBO {
  mat4 viewTM;
};

layout(location = 0) in vec3 in_position;
layout(location = 0) out vec4 out_color;

void main() {
  out_color = vec4(in_position, 1.0);
  gl_Position = viewTM * vec4(in_position, 1.0);
}
