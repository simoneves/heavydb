/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// SlangSelectionTest: not opted in to the Slang backend, so a material holding it is
// compiled by glslang even when Slang is preferred.

layout(location = 0) in vec4 in_color;
layout(location = 0) out vec4 color;

void main() {
  color = in_color;
}
