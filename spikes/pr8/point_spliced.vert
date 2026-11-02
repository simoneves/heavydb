#version 460 core
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_EXT_shader_atomic_int64 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference2  : require
#include "Marks/typeDefines.glsl"
#include "Utils/colorConvertSubroutines.glsl"
#include "Marks/coordConvertSubroutines.glsl"
#include "Marks/slabAddressTable.glsl"

// VERTEX SHADER

// RenderProperty type info defines
#define inTx double
#define inTxEnum DOUBLE
#define outTx double
#define outTxEnum DOUBLE

#define inTy double
#define inTyEnum DOUBLE
#define outTy double
#define outTyEnum DOUBLE

#define inTfillOpacity float
#define inTfillOpacityEnum FLOAT
#define outTfillOpacity float
#define outTfillOpacityEnum FLOAT

#define inTopacity float
#define inTopacityEnum FLOAT
#define outTopacity float
#define outTopacityEnum FLOAT

#define inTfillColor vec4
#define inTfillColorEnum FLOAT_VEC4
#define outTfillColor vec4
#define outTfillColorEnum FLOAT_VEC4

#define inTsize float
#define inTsizeEnum FLOAT
#define outTsize float
#define outTsizeEnum FLOAT

#define useUsize 1
#define useUfillColor 1
#define useUopacity 1
#define useUfillOpacity 1


// Vertex attribute inputs
layout (location = 0) in int64_t key;
layout (location = 1) in double x;
layout (location = 2) in double y;


// Outputs

// flat uint64_t fRowId
// flat float fPointSize
// flat vec4 fColor
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

// Color conversion declaration stubs
vec4 transformfillColorToRGB(in vec4 c) { return c; }
vec4 unpackfillColor(in uint c) { return vec4(0); }

// get* functions for properties
int64_t getkey(int64_t key) {
  return key;
}

double gety(double y) {
  return y;
}

double getx(double x) {
  return x;
}

float getsize(float size) {
  return size;
}

vec4 getfillColor(vec4 fillColor) {
  return fillColor;
}

float getopacity(float opacity) {
  return opacity;
}

float getfillOpacity(float fillOpacity) {
  return fillOpacity;
}



// project* functions for coord inputs

layout(std430) uniform PROJECTION_UBO_TYPE_x {
  dmat3x2 uProjectionViewProjMatrix;
};

double projectx(in double x_degrees){

    return (x_degrees + uProjectionViewProjMatrix[2][0]) * uProjectionViewProjMatrix[0][0] * double(viewport.width);
}

double projecty(in double y_degrees){

    double y_degrees_squared = y_degrees * y_degrees;
    double u = 3.4610923509293134e-58LF;
    u = u * y_degrees_squared + -1.5769148644800835e-53LF;
    u = u * y_degrees_squared + 3.2614389778678079e-49LF;
    u = u * y_degrees_squared + -4.0419356089559425e-45LF;
    u = u * y_degrees_squared + 3.3400810865720711e-41LF;
    u = u * y_degrees_squared + -1.9384919158744696e-37LF;
    u = u * y_degrees_squared + 8.109756477389658e-34LF;
    u = u * y_degrees_squared + -2.4706635166875875e-30LF;
    u = u * y_degrees_squared + 5.4743860481394333e-27LF;
    u = u * y_degrees_squared + -8.7044049318367135e-24LF;
    u = u * y_degrees_squared + 9.7754101107915972e-21LF;
    u = u * y_degrees_squared + -6.728626361457436e-18LF;
    u = u * y_degrees_squared + 9.438525614268527e-15LF;
    u = u * y_degrees_squared + 6.6543759286927401e-11LF;
    u = u * y_degrees_squared + 8.8621455747926686e-7LF;
    u = u * y_degrees_squared + 1.7453288065056974e-2LF;
    double log_tan = y_degrees * u;
    return (log_tan + uProjectionViewProjMatrix[2][1]) * uProjectionViewProjMatrix[1][1] * double(viewport.height);
}

// General #defines
#define numid 0
#define useKey 1
#define IS_MULTISAMPLING 1

////////////////////////////////////////////////////////////////
// Grow primitive slightly to avoid multi-sampling issues at the edges
// This is compensated in the fragment shader which shrinks the circle diameter
// by 2 pixels
const float kMultiSamplingSizePad = 2.0;

void main() {
#if useKey == 1
  if (key == invalidKey) {
    gl_Position = vec4(-10, -10, -10, 0);
    fPointSize = 0.0;
    gl_PointSize = 0.0;
    fColor = vec4(0, 0, 0, 0);
    fRowId = 0;
  } else
#endif // useKey == 1
  {
#if inTxEnum == UNSIGNED_INT64_ARB
    float px = float(projectx(getx_ptr(x, propCompressionBits, PROP_COMPRESSION_BIT_X, 0)));
#else
    float px = float(getx(projectx(x)));
#endif
#if inTyEnum == UNSIGNED_INT64_ARB
    float py = float(projecty(gety_ptr(y, propCompressionBits, PROP_COMPRESSION_BIT_Y, 0)));
#else
    float py = float(gety(projecty(y)));
#endif
    gl_Position =
        vec4(px * uViewProjMatrix[0][0] + uViewProjMatrix[2][0],
             py * uViewProjMatrix[1][1] + uViewProjMatrix[2][1],
             0.5,
             1.0);
#if IS_MULTISAMPLING == 1
    float sz = float(getsize(size)) + kMultiSamplingSizePad;
#else
    float sz = float(getsize(size));
#endif
    fPointSize = sz;
    gl_PointSize = sz;

    fColor = transformRGBtoRGB(getfillColor(fillColor));
    fColor.a *= float(getopacity(opacity)) * float(getfillOpacity(fillOpacity));

  // ids from queries go from 0 to numrows-1, but since we're storing
  // the ids as unsigned ints, and there isn't a way to specify the
  // clear value for secondary buffers, we need to account for that
  // offset here
#if numid >= 1
    fRowId = uint64_t(id + 1);
#endif
#if numid >= 2
    int total_bitshift = id_offsets[0];
    fRowId = (uint64_t(id1 + 1) << total_bitshift) | fRowId;
#endif
#if numid >= 3
    total_bitshift += id_offsets[1];
    fRowId = (uint64_t(id2 + 1) << total_bitshift) | fRowId;
#endif
  }
}
