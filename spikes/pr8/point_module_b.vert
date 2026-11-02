#version 460 core
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_EXT_shader_atomic_int64 : require
#extension GL_EXT_scalar_block_layout : require

// PR 8 follow-up spike: a second point material through the same module, to show it
// serving more than one: no key, size per vertex, unprojected, no multisampling.
import pointMark;

// Vertex attribute inputs
layout (location = 0) in double x;
layout (location = 1) in double y;
layout (location = 2) in float point_size;

// Outputs
out layout (location = 0) FragmentShaderInputs {
  flat uint64_t fRowId;
  flat vec4 fColor;
  flat float fPointSize;
};

// Uniform RenderProperties
layout(std430) uniform POINT_VERT_UBO_TYPE {
  mat3x2 uViewProjMatrix;
  uint64_t invalidKey;
  uint propCompressionBits;
  float size;
  vec4 fillColor;
  float opacity;
  float fillOpacity;
};

// viewport data
struct Viewport {
  int x;
  int y;
  int width;
  int height;
};

layout(std430, binding = 0) uniform SHARED_VIEWPORT_UBO {
  Viewport viewport;
};

// This material's properties: x, y and size per vertex, the rest uniform
struct Properties : IPointProperties {
  static bool isInvalid() { return false; }
  static double rawX() { return x; }
  static double rawY() { return y; }
  static double getX(double projected) { return projected; }
  static double getY(double projected) { return projected; }
  static float getSize() { return point_size; }
  static float4 getFillColor() { return fillColor; }
  static float getOpacity() { return opacity; }
  static float getFillOpacity() { return fillOpacity; }
};

void main() {
  PointVertex v = pointVertex<Properties, NoProjection, RGBtoRGB, false, false>(
      uViewProjMatrix[0][0], uViewProjMatrix[1][1], uViewProjMatrix[2][0],
      uViewProjMatrix[2][1]);
  gl_Position = v.position;
  gl_PointSize = v.pointSize;
  fPointSize = v.pointSize;
  fColor = v.color;
  fRowId = v.rowId;
}
