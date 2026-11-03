/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// SubroutinePrototypeTest binds subroutineBase, the only function main calls, to each
// of the functions below in turn. Only some of them have a body to call.

layout(std430, binding = 0) uniform SHARED_UBO {
  mat4 viewTM;
};

in vec3 in_position;

// Declared but never defined. Nothing calls it, so the shader still links. The
// comment in its parameter list holds a brace, which must not be taken for a body.
float subroutineProto(float x /* { */);

// Declared here and defined after main
float subroutineForward(float x);

// The type of its parameter is picked by the preprocessor, which splits the list
float subroutineSplit(
#if 1
    float x)
#else
    int x)
#endif
{
  return 2.0 * x;
}

float subroutineBase(float x) {
  return x;
}

void main() {
  float value = subroutineBase(1.0);
  gl_Position = viewTM * vec4(in_position, value);
}

float subroutineForward(float x) {
  return -x;
}
