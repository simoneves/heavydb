/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// Not coherent: only ever accessed atomically, and atomics are coherent already.
// Slang rejects an atomic on an image declared coherent.
layout(r32ui) uniform restrict uimage2D pixelCounter;

void main() {
  float d = length(vec2(gl_PointCoord.xy) * 2.0 - 1.0);
  if (d < 0.9) {
    imageAtomicAdd(pixelCounter, ivec2(gl_FragCoord.xy), 1);
  }
}
