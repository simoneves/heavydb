/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// Compiles cleanly on its own. It is here so that the stage which fails is not the
// first stage of the material, which is the case whose error used to go missing.

layout(std430, binding = 0) uniform SHARED_UBO {
  mat4 viewTM;
};

in vec3 in_position;

void main() {
  gl_Position = viewTM * vec4(in_position, 1.0);
}
