/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

//<extensions>
#extension GL_EXT_ray_tracing : require
//</extensions>

#define USE_RAYTRACED_SHADOWS 1

#include "Shading/rtTraceRay.glsl"

layout(location = 1) rayPayloadEXT bool is_shadowed;
// Bound explicitly, as every stage declaring it does: it is shared between the ray
// generation and hit stages, and Slang matches a shared resource by binding, not name
layout(binding = 0) uniform accelerationStructureEXT topLevelAS;

bool trace_shadow_ray(vec3 P, vec3 L, float tmin, float tmax) {
	is_shadowed = true; // start shadowed, miss shader will flip

	uint ray_flags = gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT;
#ifdef __SLANG__
	SLANG_TRACE_RAY(is_shadowed, topLevelAS, ray_flags, 0xFF, 0, 0, 1, P, tmin, L, tmax);
#else
	traceRayEXT(topLevelAS, ray_flags, 0xFF, 0, 0, 1, P, tmin, L, tmax, 1);
#endif
  return is_shadowed;
}
