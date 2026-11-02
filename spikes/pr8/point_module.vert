#version 460 core
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_EXT_shader_atomic_int64 : require
#extension GL_EXT_scalar_block_layout : require

// PR 8 follow-up spike: the material point_spliced.vert was assembled for, as the
// declarations it has today plus glue to the pointMark module. Everything below the
// declarations is what the Builder would generate per material instead of splicing.
import pointMark;

// Vertex attribute inputs
layout (location = 0) in int64_t key;
layout (location = 1) in double x;
layout (location = 2) in double y;

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

layout(std430) uniform PROJECTION_UBO_TYPE_x {
  dmat3x2 uProjectionViewProjMatrix;
};

// This material's properties: x and y per vertex, the rest uniform
struct Properties : IPointProperties {
  static bool isInvalid() { return key == invalidKey; }
  static double rawX() { return x; }
  static double rawY() { return y; }
  static double getX(double projected) { return projected; }
  static double getY(double projected) { return projected; }
  static float getSize() { return size; }
  static float4 getFillColor() { return fillColor; }
  static float getOpacity() { return opacity; }
  static float getFillOpacity() { return fillOpacity; }
};

// Projected with Web Mercator
struct Projection : IProjection {
  static double projectX(double v) {
    return mercatorX(v, uProjectionViewProjMatrix[2][0], uProjectionViewProjMatrix[0][0],
                     viewport.width);
  }
  static double projectY(double v) {
    return mercatorY(v, uProjectionViewProjMatrix[2][1], uProjectionViewProjMatrix[1][1],
                     viewport.height);
  }
};

void main() {
  PointVertex v = pointVertex<Properties, Projection, RGBtoRGB, true, true>(
      uViewProjMatrix[0][0], uViewProjMatrix[1][1], uViewProjMatrix[2][0],
      uViewProjMatrix[2][1]);
  gl_Position = v.position;
  gl_PointSize = v.pointSize;
  fPointSize = v.pointSize;
  fColor = v.color;
  fRowId = v.rowId;
}
