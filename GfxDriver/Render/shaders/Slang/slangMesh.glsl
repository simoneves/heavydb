/*
 * SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// Lets a GLSL mesh or task shader compile through Slang with its body unchanged.
//
// Slang's GLSL support has no mesh shader I/O: no layout(triangles, max_vertices = N)
// out, no gl_MeshVerticesEXT or primitive index arrays, no taskPayloadSharedEXT. Its own
// form takes the output vertices and primitive indices as entry point parameters, and
// the task payload as a groupshared variable in the task stage and an entry point
// parameter in the mesh stage. Under __SLANG__ this maps the GLSL names onto those, so a
// body that writes its outputs in main, as every one here does, is unchanged.
//
// A mesh shader declares its main under __SLANG__ with the output topology and workgroup
// size as attributes and SLANG_MESH_OUTPUTS(vertex type, max vertices, index type, max
// primitives) as its parameters, after the payload if it has one. The vertex type is a
// struct leading with gl_Position as SV_Position, and gl_PointSize as SV_PointSize if
// the shader writes it; GlslStructBuilder::createSlangStructString writes one. The
// shader's own per-vertex output array is the same parameter, so it defines its name as
// slang_mesh_vertices_. A task shader defines SLANG_TASK_PAYLOAD as its payload
// variable, declared groupshared.

#ifdef __SLANG__

#define SetMeshOutputsEXT(vertex_count, primitive_count) \
  SetMeshOutputCounts(vertex_count, primitive_count)
#define gl_MeshVerticesEXT slang_mesh_vertices_
#define gl_PrimitivePointIndicesEXT slang_mesh_indices_
#define gl_PrimitiveLineIndicesEXT slang_mesh_indices_
#define gl_PrimitiveTriangleIndicesEXT slang_mesh_indices_
#define EmitMeshTasksEXT(x, y, z) DispatchMesh(x, y, z, SLANG_TASK_PAYLOAD)

#define SLANG_MESH_OUTPUTS(vertex_type, max_vertices, index_type, max_primitives) \
  out vertices vertex_type slang_mesh_vertices_[max_vertices],                    \
      out indices index_type slang_mesh_indices_[max_primitives]

#endif
