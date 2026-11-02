/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// The fragment half of the pair. Its block starts where the vertex shader's ends.

layout(push_constant) uniform FRAG_SHARED_NAME_PUSH_CONSTANTS {
#ifdef __SLANG__
  [[vk::offset(4)]] uint frag_value;
#else
  layout(offset = 4) uint frag_value;
#endif
} pushConstants;

out vec4 color;

void main() {
  color = vec4(float(pushConstants.frag_value));
}
