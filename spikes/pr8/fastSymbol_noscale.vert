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
#include "Marks/fastSymbolDefines.glsl"
import quantitativeScale;

// VERTEX SHADER

// RenderProperty type info defines
#define inTstrokeOpacity float
#define inTstrokeOpacityEnum FLOAT
#define outTstrokeOpacity float
#define outTstrokeOpacityEnum FLOAT

#define inTwidth float
#define inTwidthEnum FLOAT
#define outTwidth float
#define outTwidthEnum FLOAT

#define inTopacity float
#define inTopacityEnum FLOAT
#define outTopacity float
#define outTopacityEnum FLOAT

#define inTstrokeWidth float
#define inTstrokeWidthEnum FLOAT
#define outTstrokeWidth float
#define outTstrokeWidthEnum FLOAT

#define inTx float
#define inTxEnum FLOAT
#define outTx float
#define outTxEnum FLOAT

#define inTxc double
#define inTxcEnum DOUBLE
#define outTxc float
#define outTxcEnum FLOAT

#define inTy float
#define inTyEnum FLOAT
#define outTy float
#define outTyEnum FLOAT

#define inTx2 float
#define inTx2Enum FLOAT
#define outTx2 float
#define outTx2Enum FLOAT

#define inTstrokeColor vec4
#define inTstrokeColorEnum FLOAT_VEC4
#define outTstrokeColor vec4
#define outTstrokeColorEnum FLOAT_VEC4

#define inTfillOpacity float
#define inTfillOpacityEnum FLOAT
#define outTfillOpacity float
#define outTfillOpacityEnum FLOAT

#define inTy2 float
#define inTy2Enum FLOAT
#define outTy2 float
#define outTy2Enum FLOAT

#define inTyc double
#define inTycEnum DOUBLE
#define outTyc float
#define outTycEnum FLOAT

#define inTheight float
#define inTheightEnum FLOAT
#define outTheight float
#define outTheightEnum FLOAT

#define inTshape int
#define inTshapeEnum INT
#define outTshape int
#define outTshapeEnum INT

#define inTfillColor vec4
#define inTfillColorEnum FLOAT_VEC4
#define outTfillColor vec4
#define outTfillColorEnum FLOAT_VEC4

#define useUfillColor 1
#define useUshape 1
#define useUfillOpacity 1
#define useUy2 1
#define useUstrokeColor 1
#define useUx2 1
#define useUy 1
#define useUheight 1
#define useUx 1
#define useUstrokeWidth 1
#define useUopacity 1
#define useUwidth 1
#define useUstrokeOpacity 1


// Vertex attribute inputs
layout (location = 0) in int64_t key;
layout (location = 1) in double xc;
layout (location = 2) in double yc;


// Outputs

// flat uint64_t fRowId
// flat uint fShapeType
// flat vec4 fSymbolScale
// flat float fPointSize
// flat vec4 fFillColor
// flat vec4 fStrokeColor
// flat float fStrokeWidth
// flat float fApproxMaxCoverage
// flat int accumIdx (accumulation render only)
out layout (location = 0) FragmentShaderInputs {
  flat uint64_t fRowId;
  flat uint fShapeType;
  flat vec4 fSymbolScale;
  flat float fPointSize;
  flat vec4 fFillColor;
  flat vec4 fStrokeColor;
  flat float fStrokeWidth;
  flat float fApproxMaxCoverage;
};

// Uniform RenderProperties
layout(std430) uniform FAST_SYMBOL_VERT_UBO_TYPE {
  mat3x2 uVPmatrix;
  float uPivotx;
  float uPivoty;
  uint64_t invalidKey;
  uint propCompressionBits;
  vec4 fillColor;
  int shape;
  float fillOpacity;
  float y2;
  vec4 strokeColor;
  float x2;
  float y;
  float height;
  float x;
  float strokeWidth;
  float opacity;
  float width;
  float strokeOpacity;
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

// Color conversion stubs
vec4 transformfillColorToRGB(in vec4 c) { return c; }
vec4 transformstrokeColorToRGB(in vec4 c) { return c; }
vec4 unpackfillColor(in uint c) { return vec4(0); }
vec4 unpackstrokeColor(in uint c) { return vec4(0); }

// get* functions for properties
int64_t getkey(int64_t key) {
  return key;
}


/*
 * SPDX-FileCopyrightText: Copyright (c) 2015-2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// SCALE y_yc, from the quantitativeScale module
#define domainType_y_yc double
#define domainTypeEnum_y_yc DOUBLE
#define rangeType_y_yc float
#define rangeTypeEnum_y_yc FLOAT
#define numDomains_y_yc 2
#define numRanges_y_yc 2
#define useClamp_y_yc 0
#define doAccum_QuantitativeScale_y_yc 0

layout(std430) uniform QUANTITATIVE_SCALE_UBO_TYPE_y_yc {
  domainType_y_yc uDomains_y_yc[numDomains_y_yc];
  domainType_y_yc nullDomainVal_y_yc;
  rangeType_y_yc uRanges_y_yc[numRanges_y_yc];
  rangeType_y_yc nullRangeVal_y_yc;
  float uExponent_y_yc;
};

float evalQuantitativeScale_y_yc(in double domainVal) {
  return float(domainVal);
}



/*
 * SPDX-FileCopyrightText: Copyright (c) 2015-2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// SCALE x_xc, from the quantitativeScale module
#define domainType_x_xc double
#define domainTypeEnum_x_xc DOUBLE
#define rangeType_x_xc float
#define rangeTypeEnum_x_xc FLOAT
#define numDomains_x_xc 2
#define numRanges_x_xc 2
#define useClamp_x_xc 0
#define doAccum_QuantitativeScale_x_xc 0

layout(std430) uniform QUANTITATIVE_SCALE_UBO_TYPE_x_xc {
  domainType_x_xc uDomains_x_xc[numDomains_x_xc];
  domainType_x_xc nullDomainVal_x_xc;
  rangeType_x_xc uRanges_x_xc[numRanges_x_xc];
  rangeType_x_xc nullRangeVal_x_xc;
  float uExponent_x_xc;
};

float evalQuantitativeScale_x_xc(in double domainVal) {
  return float(domainVal);
}


vec4 getfillColor(vec4 fillColor) {
  return fillColor;
}

int getshape(int shape) {
  return shape;
}

float getfillOpacity(float fillOpacity) {
  return fillOpacity;
}

float gety2(float y2) {
  return y2;
}

vec4 getstrokeColor(vec4 strokeColor) {
  return strokeColor;
}

float getx2(float x2) {
  return x2;
}

float gety(float y) {
  return y;
}

float getheight(float height) {
  return height;
}

float getx(float x) {
  return x;
}

float getstrokeWidth(float strokeWidth) {
  return strokeWidth;
}

float getopacity(float opacity) {
  return opacity;
}

float getwidth(float width) {
  return width;
}

float getstrokeOpacity(float strokeOpacity) {
  return strokeOpacity;
}



// project* functions for coord inputs
inTx projectx(in inTx x) {
  return x;
}

inTx2 projectx2(in inTx2 x) {
  return x;
}

inTxc projectxc(in inTxc x) {
  return x;
}

inTy projecty(in inTy y) {
  return y;
}

inTy2 projecty2(in inTy2 y) {
  return y;
}

inTyc projectyc(in inTyc y) {
  return y;
}

// General #defines
#define numid 0
#define useKey 1
#define doHeatmapEdgePad 0

#define computeX 2
#define computeY 2
#define computeWidth 0
#define computeHeight 0

void processEmptyVertex() {
  gl_Position = vec4(-10, -10, -10, 0);
  fShapeType = 0;
  fPointSize = 0.0;
}

void processVertex() {
  fShapeType = uint(getshape(shape));

#if computeX == 0
#if inTxEnum == UNSIGNED_INT64_ARB
  float finalxPos = float(projectx(getx_ptr(x, propCompressionBits, PROP_COMPRESSION_BIT_X, 0)));
#else
  float finalxPos = float(getx(x));
#endif
#elif computeX == 1
#if inTx2Enum == UNSIGNED_INT64_ARB
  float finalxPos = float(projectx2(getx_ptr(x2, propCompressionBits, PROP_COMPRESSION_BIT_X2, 0)));
#else
  float finalxPos = float(getx2(x2));
#endif
#else
#if inTxcEnum == UNSIGNED_INT64_ARB
  float finalxPos = float(projectxc(getx_ptr(xc, propCompressionBits, PROP_COMPRESSION_BIT_XC, 0)));
#else
  float finalxPos = float(evalQuantitativeScale_x_xc(xc));
#endif
#endif  // computeX
#if computeY == 0
#if inTyEnum == UNSIGNED_INT64_ARB
  float finalyPos = float(projecty(gety_ptr(y, propCompressionBits, PROP_COMPRESSION_BIT_Y, 0)));
#else
  float finalyPos = float(gety(y));
#endif
#elif computeY == 1
#if inTy2Enum == UNSIGNED_INT64_ARB
  float finalyPos = float(projecty2(gety_ptr(y2, propCompressionBits, PROP_COMPRESSION_BIT_Y2, 0)));
#else
  float finalyPos = float(gety2(y2));
#endif
#else
#if inTycEnum == UNSIGNED_INT64_ARB
  float finalyPos = float(projectyc(gety_ptr(yc, propCompressionBits, PROP_COMPRESSION_BIT_YC, 0)));
#else
  float finalyPos = float(evalQuantitativeScale_y_yc(yc));
#endif
#endif  // computeY

  vec2 size;
#if computeWidth == 0
  size.x = float(getwidth(width));
#else
#if inTx2Enum == UNSIGNED_INT64_ARB
  size.x = float(projectx2(getx_ptr(x2, propCompressionBits, PROP_COMPRESSION_BIT_X2, 0))) - finalxPos;
#else
  size.x = float(getx2(x2)) - finalxPos;
#endif
#endif  // computeWidth
#if computeHeight == 0
  size.y = float(getheight(height));
#else
#if inTy2Enum == UNSIGNED_INT64_ARB
  size.y = float(projecty2(gety_ptr(y2, propCompressionBits, PROP_COMPRESSION_BIT_Y2, 0))) - finalyPos;
#else
  size.y = float(gety2(y2)) - finalyPos;
#endif
#endif  // computeHeight

  // Cull if too small. This will catch NULLS
  if (max(size.x, size.y) < CULL_SIZE) {
    processEmptyVertex();
    return;
  }

  // Grow the point slightly in order to guarantee there are no subpixel gaps between the
  // points when rendering binned heatmaps
#if doHeatmapEdgePad == 1
  size += 0.1;
#endif // doHeatmapEdgePad

  float maxSize = max(size.x, size.y);
  float useStrokeWidth = float(getstrokeWidth(strokeWidth));

  // determine the approximate maximum pixel coverage for the symbol. This is needed
  // for sizes < 1.0
  // TODO: incorporate stroke width?
  fApproxMaxCoverage = min(1.0, size.x) * min(1.0, size.y);

  // always round up otherwise we'll lose strokes that are < 0.5 pixels
  // on shapes that abut the edge of the quad
  fPointSize = maxSize + ceil(useStrokeWidth);

  // TODO: uPivotx/y can be premultiplied
  float pivot_y = fShapeType == WEDGE ? uPivoty + 0.3333 : uPivoty;
  gl_Position = vec4(uPivotx * uVPmatrix[0][0] * size.x + uVPmatrix[0][0] * finalxPos + uVPmatrix[2][0],
                     pivot_y * uVPmatrix[1][1] * size.y + uVPmatrix[1][1] * finalyPos + uVPmatrix[2][1],
                     0.0, 1.0);

  float point_size_padded = fPointSize + NUM_PAD_PIXELS;
  if (fShapeType == CIRCLE) {
    fSymbolScale = vec4(point_size_padded / size.x, point_size_padded / size.y,
                        size.x / point_size_padded, size.y / point_size_padded);
  } else {
    float d = 1.0 / point_size_padded;
    fSymbolScale = vec4(d * size.x, d * size.y, 0.0, 0.0);
  }
  gl_PointSize = point_size_padded;

  fStrokeWidth = useStrokeWidth;
  fFillColor = transformRGBtoRGB(getfillColor(fillColor));
  fFillColor.a *= float(getopacity(opacity)) * float(getfillOpacity(fillOpacity));

  fStrokeColor = transformRGBtoRGB(getstrokeColor(strokeColor));
  fStrokeColor.a *= float(getopacity(opacity)) * float(getstrokeOpacity(strokeOpacity));
}

void main() {
#if useKey == 1
  if (key != invalidKey) {
    processVertex();
  } else {
    processEmptyVertex();
  }
#else
  processVertex();
#endif  // useKey

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
