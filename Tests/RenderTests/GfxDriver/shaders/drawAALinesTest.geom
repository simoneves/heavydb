/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */



#define LINE_JOIN_BEVEL 0
#define LINE_JOIN_ROUND 1
#define LINE_JOIN_MITER 2

#define NO_DIST_COORDS vec2(0.0)

// See Slang/slangGeometry.glsl
struct GeometryIn {
  float4 gl_Position : SV_Position;
  [[vk::location(0)]] vec4 gColor;
  [[vk::location(1)]] float gStrokeWidth;
  [[vk::location(2)]] nointerpolation int gLineJoin;
  [[vk::location(3)]] float gMiterLimit;
};
#define GeometryIn_MEMBERS(X) \
  X(vec4, gColor) X(float, gStrokeWidth) X(int, gLineJoin) X(float, gMiterLimit)
struct GeometryOut {
  float4 gl_Position : SV_Position;
  [[vk::location(0)]] vec2 fNormDistCoords;
  [[vk::location(1)]] nointerpolation vec4 fColor;
};
#define GeometryOut_MEMBERS(X) X(vec2, fNormDistCoords) X(vec4, fColor)
#define SLANG_GEOMETRY_IN GeometryIn
#define SLANG_GEOMETRY_IN_MEMBERS GeometryIn_MEMBERS
#define SLANG_GEOMETRY_IN_COUNT 4
#define SLANG_GEOMETRY_OUT GeometryOut
#define SLANG_GEOMETRY_OUT_MEMBERS GeometryOut_MEMBERS
#include "Slang/slangGeometry.glsl"

void emitVertex(
  in vec2 pos,
  in vec2 zw,
  in vec2 norm_dist_coords,
  in vec4 color
  SLANG_GEOMETRY_STREAM_EXTRA) {
  gl_Position = vec4(pos, zw);
  fNormDistCoords = norm_dist_coords;
  fColor = color;
  EmitVertex();
}

vec2 orthogonal(in vec2 v) {
  return vec2(v.y, -v.x);
}

float cross(in vec2 v1, in vec2 v2) {
  return (v1.x * v2.y) - (v1.y * v2.x);
}

void renderStroke(
  in vec2 pos0,
  in vec2 pos1,
  in vec2 pos2,
  in vec2 zw1,
  in vec2 zw2,
  in vec4 color,
  in float line_width,
  in int line_join,
  in float miter_limit
  SLANG_GEOMETRY_STREAM_EXTRA) {
  // main segment
  vec2 segment_dir = pos2 - pos1;
  float segment_len = length(segment_dir);
  if (segment_len > 0.0) {
    // worth drawing this segment
    segment_dir = segment_dir / segment_len;

    // previous segment
    vec2 prev_segment_dir = pos1 - pos0;
    float prev_segment_dir_len = length(prev_segment_dir);
    if (prev_segment_dir_len > 0.0) {
      prev_segment_dir = prev_segment_dir / prev_segment_dir_len;
    } else {
      prev_segment_dir = segment_dir;
    }

    // main segment
    vec2 segment_norm = orthogonal(segment_dir);

    // line widths
    float line_half_width = 0.5 * line_width;
    vec2 line_half_width_offset = segment_norm * line_half_width;

    // the four corners of the main segment
    vec2 pos1a = pos1 + line_half_width_offset;
    vec2 pos1b = pos1 - line_half_width_offset;
    vec2 pos2a = pos2 + line_half_width_offset;
    vec2 pos2b = pos2 - line_half_width_offset;

    // draw the joint with the previous segment
    vec2 prev_segment_norm = orthogonal(prev_segment_dir);
    if (line_join == LINE_JOIN_BEVEL) {
      // single triangle to fill bevel
      if (cross(prev_segment_dir, segment_dir) > 0.0) {
        // right turn
        vec2 prev_pos2a = pos1 + (line_half_width * prev_segment_norm);
        emitVertex(prev_pos2a, zw1, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
        emitVertex(pos1a, zw1, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
        emitVertex(pos1, zw1, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
        EndPrimitive();
      } else {
        // left turn
        vec2 prev_pos2b = pos1 - (line_half_width * prev_segment_norm);
        emitVertex(prev_pos2b, zw1, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
        emitVertex(pos1, zw1, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
        emitVertex(pos1b, zw1, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
        EndPrimitive();
      }
    } else {
      // two triangles for miter or round
      vec2 half_norm_sum = prev_segment_norm + segment_norm;
      float half_norm_sum_length = length(half_norm_sum);
      // skip joint if segment doubles back on itself
      if (half_norm_sum_length > 0.0) {
        vec2 half_norm = half_norm_sum / half_norm_sum_length;
        float inv_miter_limit = 1.0 / clamp(miter_limit, 1.0, 10.0);
        float half_dot_limited = max(dot(half_norm, segment_norm), inv_miter_limit);
        vec2 miter_pos_norm = half_norm / half_dot_limited;
        if (cross(prev_segment_dir, segment_dir) > 0.0) {
          // right turn
          vec2 prev_pos2a_norm = prev_segment_norm;
          vec2 pos1a_norm = segment_norm;
          vec2 prev_pos2a = pos1 + (line_half_width * prev_pos2a_norm);
          vec2 miter_pos = pos1 + (line_half_width * miter_pos_norm);
          if (line_join == LINE_JOIN_ROUND) {
            // draw with coords for clipping
            emitVertex(prev_pos2a, zw1, prev_pos2a_norm, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
            emitVertex(miter_pos, zw1, miter_pos_norm, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
            emitVertex(pos1, zw1, vec2(0.0), color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
            emitVertex(pos1a, zw1, pos1a_norm, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
            EndPrimitive();
          } else {
            // draw with no coords
            emitVertex(prev_pos2a, zw1, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
            emitVertex(miter_pos, zw1, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
            emitVertex(pos1, zw1, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
            emitVertex(pos1a, zw1, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
            EndPrimitive();
          }
        } else {
          // left turn
          vec2 prev_pos2b_norm = prev_segment_norm;
          vec2 pos1b_norm = segment_norm;
          vec2 prev_pos2b = pos1 - (line_half_width * prev_pos2b_norm);
          vec2 miter_pos = pos1 - (line_half_width * miter_pos_norm);
          if (line_join == LINE_JOIN_ROUND) {
            // draw with coords for clipping
            emitVertex(prev_pos2b, zw1, prev_pos2b_norm, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
            emitVertex(pos1, zw1, vec2(0.0), color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
            emitVertex(miter_pos, zw1, miter_pos_norm, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
            emitVertex(pos1b, zw1, pos1b_norm, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
            EndPrimitive();
          } else {
            // draw with no coords
            emitVertex(prev_pos2b, zw1, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
            emitVertex(pos1, zw1, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
            emitVertex(miter_pos, zw1, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
            emitVertex(pos1b, zw1, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
            EndPrimitive();
          }
        }
      }
    }

    // draw the main segment
    emitVertex(pos1a, zw1, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
    emitVertex(pos2a, zw2, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
    emitVertex(pos1b, zw1, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
    emitVertex(pos2b, zw2, NO_DIST_COORDS, color SLANG_GEOMETRY_STREAM_EXTRA_ARG);
    EndPrimitive();
  }
}

void geometry_main(SLANG_GEOMETRY_STREAM) {
  renderStroke(
    gl_in[0].gl_Position.xy,
    gl_in[1].gl_Position.xy,
    gl_in[2].gl_Position.xy,
    gl_in[1].gl_Position.zw,
    gl_in[2].gl_Position.zw,
    gColor[1],
    gStrokeWidth[1],
    gLineJoin[1],
    gMiterLimit[1]
    SLANG_GEOMETRY_STREAM_EXTRA_ARG);
}

[maxvertexcount(8)]
void main(lineadj GeometryIn slang_geometry_input_[4], SLANG_GEOMETRY_STREAM) {
  SLANG_GEOMETRY_LOAD_INPUTS();
  geometry_main(SLANG_GEOMETRY_STREAM_ARG);
}
