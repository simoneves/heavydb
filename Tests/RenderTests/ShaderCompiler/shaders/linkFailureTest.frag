/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// Parses, but has no main, which is only rejected once the entry point is looked for.

out vec4 color;

vec4 not_main() {
  return vec4(1.0);
}
