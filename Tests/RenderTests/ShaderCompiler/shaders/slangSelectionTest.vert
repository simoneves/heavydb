/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// CompilerBackendSelectionTest: compiled by whichever compiler the ShaderManager is
// set to, and reflected the same by both.

layout(std430, binding = 0) uniform SELECTION_UBO {
  mat4 viewTM;
};

layout(location = 0) in vec3 in_position;
layout(location = 0) out vec4 out_color;

void main() {
  out_color = vec4(in_position, 1.0);
  gl_Position = viewTM * vec4(in_position, 1.0);
}
