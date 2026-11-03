/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// SubroutineSwapTest binds subroutineA to subroutineB and subroutineB to subroutineA.
// Each call has its own distinctive argument, so the test can tell from the generated
// GLSL which function each one ended up calling.

layout(std430, binding = 0) uniform SHARED_UBO {
  mat4 viewTM;
};

in vec3 in_position;

float subroutineA(float x) {
  return x;
}

float subroutineB(float x) {
  return -x;
}

void main() {
  float first = subroutineA(1.0);
  float second = subroutineB(2.0);
  gl_Position = viewTM * vec4(in_position, first + second);
}
