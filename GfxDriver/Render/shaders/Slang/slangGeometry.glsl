/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// Lets a GLSL geometry shader compile through Slang with its body unchanged.
//
// Slang's GLSL support has no geometry shader I/O: no layout(points) in or
// layout(triangle_strip) out, no unsized input arrays, no EmitVertex. Slang's own form
// is an entry point taking the input primitive as an array of structs and an output
// stream, appending output structs to the stream. This declares the
// inputs and outputs as the globals the GLSL body reads and writes, gl_in included, and
// defines EmitVertex and EndPrimitive in terms of the stream.
//
// The stream has to reach every function that emits a vertex, so a function that does
// declares SLANG_GEOMETRY_STREAM as its sole parameter, or SLANG_GEOMETRY_STREAM_EXTRA
// after its others, and its callers pass SLANG_GEOMETRY_STREAM_ARG or
// SLANG_GEOMETRY_STREAM_EXTRA_ARG.
//
// Before including this, a shader defines:
//   SLANG_GEOMETRY_IN            the struct of one input vertex, which leads with
//                                gl_Position as SV_Position and then has the varyings
//   SLANG_GEOMETRY_IN_MEMBERS(X) X(type, name) for each of those varyings
//   SLANG_GEOMETRY_IN_COUNT      the input primitive's vertex count
//   SLANG_GEOMETRY_OUT, SLANG_GEOMETRY_OUT_MEMBERS(X)  the same for an output vertex
// GlslStructBuilder::createSlangStructString writes the structs and the member lists.
// Its entry point, main, then calls SLANG_GEOMETRY_LOAD_INPUTS() before anything else,
// naming its input array slang_geometry_input_ and its stream SLANG_GEOMETRY_STREAM.

#define SLANG_GEOMETRY_DECLARE_IN_(type, name) static type name[SLANG_GEOMETRY_IN_COUNT];
SLANG_GEOMETRY_IN_MEMBERS(SLANG_GEOMETRY_DECLARE_IN_)

struct SlangGeometryPerVertex {
  vec4 gl_Position;
};
static SlangGeometryPerVertex gl_in[SLANG_GEOMETRY_IN_COUNT];

#define SLANG_GEOMETRY_DECLARE_OUT_(type, name) static type name;
SLANG_GEOMETRY_OUT_MEMBERS(SLANG_GEOMETRY_DECLARE_OUT_)

#define SLANG_GEOMETRY_LOAD_IN_(type, name) \
  name[slang_geometry_i_] = slang_geometry_input_[slang_geometry_i_].name;
#define SLANG_GEOMETRY_LOAD_INPUTS()                                                \
  for (int slang_geometry_i_ = 0; slang_geometry_i_ < SLANG_GEOMETRY_IN_COUNT;     \
       ++slang_geometry_i_) {                                                       \
    gl_in[slang_geometry_i_].gl_Position =                                          \
        slang_geometry_input_[slang_geometry_i_].gl_Position;                       \
    SLANG_GEOMETRY_IN_MEMBERS(SLANG_GEOMETRY_LOAD_IN_)                              \
  }

#define SLANG_GEOMETRY_STORE_OUT_(type, name) slang_geometry_vertex_.name = name;
#define EmitVertex()                                          \
  {                                                           \
    SLANG_GEOMETRY_OUT slang_geometry_vertex_;                \
    slang_geometry_vertex_.gl_Position = gl_Position;         \
    SLANG_GEOMETRY_OUT_MEMBERS(SLANG_GEOMETRY_STORE_OUT_)     \
    slang_geometry_stream_.Append(slang_geometry_vertex_);    \
  }
#define EndPrimitive() slang_geometry_stream_.RestartStrip()

#define SLANG_GEOMETRY_STREAM \
  inout TriangleStream<SLANG_GEOMETRY_OUT> slang_geometry_stream_
#define SLANG_GEOMETRY_STREAM_EXTRA , SLANG_GEOMETRY_STREAM
#define SLANG_GEOMETRY_STREAM_ARG slang_geometry_stream_
#define SLANG_GEOMETRY_STREAM_EXTRA_ARG , slang_geometry_stream_

