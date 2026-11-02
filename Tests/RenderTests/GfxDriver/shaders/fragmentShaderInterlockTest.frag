/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __SLANG__
#extension GL_ARB_fragment_shader_interlock : require
#endif

#include <DriverTests/testUtils.glsl>

#ifndef __SLANG__
layout(pixel_interlock_ordered) in;
#endif

layout(location = 0) in float in_key;

#ifdef __SLANG__
// Slang's GLSL support has neither the interlock layout nor the ARB functions, and
// rejects any access to a coherent image. A rasterizer-ordered texture is its form of
// the same thing: Slang wraps the accesses in an ordered pixel interlock itself. It
// still needs globallycoherent, Slang's coherent, or the test renders wrong.
[format("rgba8")] globallycoherent RasterizerOrderedTexture2D<float4> output_image;
#define beginInvocationInterlockARB()
#define endInvocationInterlockARB()
#define imageLoad(image, coord) image[coord]
#define imageStore(image, coord, value) image[coord] = (value)
#else
// both coherent and the interlock are required to guarantee memory ordering
layout (rgba8) coherent uniform image2D output_image;
#endif

vec4 blend_over(vec4 fore, vec4 back) {
	return fore + (1.0 - fore.a) * vec4(back.rgb * back.a, back.a);
}

void main(void) {
  beginInvocationInterlockARB();
  vec4 current_color = imageLoad(output_image, ivec2(gl_FragCoord.xy));

  // do a little work between load and store to increase likelyhood of sync issues
  const float alpha = 0.2;
  vec4 color = transformHSLtoRGB(vec4(in_key * 360.0, 1.0, 0.5, alpha));
  vec4 comped_color = blend_over(vec4(color.rgb, alpha), current_color);

  imageStore(output_image, ivec2(gl_FragCoord.xy), comped_color);
  endInvocationInterlockARB();
}
