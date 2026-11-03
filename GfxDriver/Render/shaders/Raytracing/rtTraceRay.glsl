/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// Slang's GLSL traceRayEXT does not compile: Slang aborts with an internal error in
// emitSPIRVAsm. Its own TraceRay does, given the payload variable rather than the
// payload's location, and copies the payload in and out around the trace. A shader that
// traces rays defines traceRayEXT in terms of SLANG_TRACE_RAY, naming its payload, so
// that its body can keep calling traceRayEXT.
#define SLANG_TRACE_RAY(payload,                                                       \
                        acceleration_structure,                                        \
                        ray_flags,                                                     \
                        cull_mask,                                                     \
                        sbt_record_offset,                                             \
                        sbt_record_stride,                                             \
                        miss_index,                                                    \
                        origin,                                                        \
                        tmin,                                                          \
                        direction,                                                     \
                        tmax)                                                          \
  {                                                                                    \
    RayDesc trace_ray_desc_;                                                           \
    trace_ray_desc_.Origin = (origin);                                                 \
    trace_ray_desc_.TMin = (tmin);                                                     \
    trace_ray_desc_.Direction = (direction);                                           \
    trace_ray_desc_.TMax = (tmax);                                                     \
    TraceRay(acceleration_structure,                                                   \
             ray_flags,                                                                \
             cull_mask,                                                                \
             sbt_record_offset,                                                        \
             sbt_record_stride,                                                        \
             miss_index,                                                               \
             trace_ray_desc_,                                                          \
             payload);                                                                 \
  }
