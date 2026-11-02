// PR 8 follow-up spike: the point Mark's vertex stage as a module, checked once per
// session, with everything that varies between materials expressed as generic
// parameters rather than spliced text.
//
// The GLSL template, Marks/shaders/pointTemplate.vert, is assembled per material: the
// Builder fills in property types, splices in getters, scales and a projection, and
// #if selects key handling, row IDs and multisampling. Here the body is written once.
// A material supplies only its declarations (vertex attributes and uniform blocks,
// unchanged, so reflection is unchanged) and two small types implementing
// IPointProperties and IProjection against them, then calls pointVertex.
//
// Covers the variant be-render-tests captured plus the obvious neighbours: properties
// from attributes or uniforms, with or without a projection and a key. Not covered:
// row IDs, slab (buffer address) properties, scales and accumulation.
module pointMark;

// GLSL's built-in names, which a .slang module does not see by default
import glsl;

#include "Marks/typeDefines.glsl"
#include "Utils/colorConvertSubroutines.glsl"
#include "Marks/coordConvertSubroutines.glsl"

// What a point vertex shader produces
public struct PointVertex
{
    public float4 position;
    public float pointSize;
    public float4 color;
    public uint64_t rowId;
}

// Where a material's properties come from, and how each raw value becomes the value
// drawn. A material's implementation reads its own attributes and uniforms.
public interface IPointProperties
{
    static bool isInvalid();
    static double rawX();
    static double rawY();
    static double getX(double projected);
    static double getY(double projected);
    static float getSize();
    static float4 getFillColor();
    static float getOpacity();
    static float getFillOpacity();
}

// How x and y are projected before the view-projection matrix is applied
public interface IProjection
{
    static double projectX(double x);
    static double projectY(double y);
}

public struct NoProjection : IProjection
{
    public static double projectX(double x) { return x; }
    public static double projectY(double y) { return y; }
}

// Web Mercator, as the Builder splices in for a projected coordinate. The material's
// projection type supplies its matrix terms and viewport size.
public double mercatorX(double x_degrees, double offset, double scale, int width)
{
    return (x_degrees + offset) * scale * double(width);
}

public double mercatorY(double y_degrees, double offset, double scale, int height)
{
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
    return (log_tan + offset) * scale * double(height);
}

// The colour space conversion applied to the fill colour
public interface IColorTransform
{
    static float4 apply(float4 color);
}

public struct RGBtoRGB : IColorTransform
{
    public static float4 apply(float4 color) { return transformRGBtoRGB(color); }
}

// Grow the primitive slightly to avoid multisampling issues at the edges, which the
// fragment shader compensates for by shrinking the circle
static const float kMultiSamplingSizePad = 2.0;

// The template's main. view_proj_* are the terms of the view-projection matrix that
// it reads: [0][0], [1][1], [2][0] and [2][1] of the GLSL mat3x2.
public PointVertex pointVertex<
    P : IPointProperties,
    Proj : IProjection,
    Color : IColorTransform,
    let UseKey : bool,
    let Multisampling : bool>(
    float view_proj_00, float view_proj_11, float view_proj_20, float view_proj_21)
{
    PointVertex v;
    if (UseKey && P.isInvalid())
    {
        v.position = float4(-10, -10, -10, 0);
        v.pointSize = 0.0;
        v.color = float4(0, 0, 0, 0);
        v.rowId = 0;
        return v;
    }
    float px = float(P.getX(Proj.projectX(P.rawX())));
    float py = float(P.getY(Proj.projectY(P.rawY())));
    v.position = float4(px * view_proj_00 + view_proj_20,
                        py * view_proj_11 + view_proj_21,
                        0.5,
                        1.0);
    float size = P.getSize();
    if (Multisampling)
    {
        size += kMultiSamplingSizePad;
    }
    v.pointSize = size;
    v.color = Color.apply(P.getFillColor());
    v.color.a *= P.getOpacity() * P.getFillOpacity();
    v.rowId = 0;
    return v;
}
