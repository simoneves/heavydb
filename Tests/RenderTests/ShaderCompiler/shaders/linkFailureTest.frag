/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// Parses, but has no main, which glslang only rejects when it links the stage.

out vec4 color;

vec4 not_main() {
  return vec4(1.0);
}
