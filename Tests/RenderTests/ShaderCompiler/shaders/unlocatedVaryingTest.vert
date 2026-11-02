/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

layout(std430, binding = 0) uniform SHARED_UBO {
  mat4 viewTM;
};

// A vertex input needs no location of its own: it is matched against the reflection
in vec3 in_position;

layout(location = 0) out float out_depth;

// Deliberately without locations, which is what this test is for, and declared in the
// opposite order to the fragment shader's inputs. Each compiler would assign them by
// declaration order, so the two stages would disagree without a word; the compile has
// to fail rather than assign them and hope.
out vec3 out_colour;
out float out_alpha;

void main() {
  gl_Position = viewTM * vec4(in_position, 1.0);
  out_depth = in_position.z;
  out_colour = in_position * 0.5 + 0.5;
  out_alpha = 0.5;
}
