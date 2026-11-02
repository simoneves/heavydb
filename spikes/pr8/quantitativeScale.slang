// PR 8 spike: the quantitative scale as a generic Slang module.
//
// The GLSL template, Scales/shaders/quantitativeScaleTemplate.vert, is text instantiated
// once per property: its types are filled in by tags, its variants chosen by #if on
// type enums, and its "subroutines" are stub functions the Builder retargets to the
// chosen transform, null check and interpolation. Here the same logic is written once:
// the types are generic parameters, and the choices are interface implementations
// passed as type arguments.
//
// The scale's uniform block stays in the material's own shader, declared as today, so
// its reflected names do not change; its arrays are passed in. Only floating-point
// domains and ranges are covered, which is enough for the spike.
module quantitativeScale;

//
// Transforms, applied to the domain value before interpolation
//
public interface IQuantTransform<D : __BuiltinFloatingPointType>
{
    static D apply(D value, float exponent);
}

public struct PassThruTransform<D : __BuiltinFloatingPointType> : IQuantTransform<D>
{
    public static D apply(D value, float exponent) { return value; }
}

public struct LogTransform<D : __BuiltinFloatingPointType> : IQuantTransform<D>
{
    public static D apply(D value, float exponent) { return log(value); }
}

public struct PowTransform<D : __BuiltinFloatingPointType> : IQuantTransform<D>
{
    public static D apply(D value, float exponent) { return pow(value, __realCast<D>(exponent)); }
}

public struct SqrtTransform<D : __BuiltinFloatingPointType> : IQuantTransform<D>
{
    public static D apply(D value, float exponent) { return sqrt(value); }
}

//
// Null handling
//
public interface INullCheck<D : __BuiltinFloatingPointType>
{
    static bool isNull(D value, D null_value);
}

public struct NoNullCheck<D : __BuiltinFloatingPointType> : INullCheck<D>
{
    public static bool isNull(D value, D null_value) { return false; }
}

public struct NullCheck<D : __BuiltinFloatingPointType> : INullCheck<D>
{
    public static bool isNull(D value, D null_value) { return value == null_value; }
}

//
// Interpolation between two range values
//
public interface IQuantInterp<R : __BuiltinFloatingPointType, D : __BuiltinFloatingPointType>
{
    static R interp(R v1, R v2, D t);
}

public struct DefaultInterp<R : __BuiltinFloatingPointType, D : __BuiltinFloatingPointType>
    : IQuantInterp<R, D>
{
    public static R interp(R v1, R v2, D t) { return lerp(v1, v2, __realCast<R>(t)); }
}

//
// The scale itself
//
public R evalQuantitativeScale<
    D : __BuiltinFloatingPointType,
    R : __BuiltinFloatingPointType,
    let NumDomains : int,
    let NumRanges : int,
    Transform : IQuantTransform<D>,
    Null : INullCheck<D>,
    Interp : IQuantInterp<R, D>,
    let UseClamp : bool>(
    D domains[NumDomains],
    D null_domain,
    R ranges[NumRanges],
    R null_range,
    float exponent,
    D domain_value)
{
    if (Null.isNull(domain_value, null_domain))
    {
        return null_range;
    }
    D value = Transform.apply(domain_value, exponent);

    int idx1 = 0;
    int idx2 = 0;
    if (NumDomains == 1 || NumRanges == 1)
    {
        idx1 = 0;
        idx2 = 0;
    }
    else if (NumDomains == 2 || NumRanges == 2)
    {
        idx1 = 0;
        idx2 = 1;
    }
    else
    {
        // Binary search for the two domain values bracketing the value
        int start = 0;
        int end = NumDomains - 1;
        if (value <= domains[start])
        {
            idx1 = start;
            idx2 = start + 1;
        }
        else if (value >= domains[end])
        {
            idx1 = end - 1;
            idx2 = end;
        }
        else
        {
            while (true)
            {
                int mid = start + (end - start) / 2;
                if (mid == start)
                {
                    idx1 = mid;
                    idx2 = end;
                    break;
                }
                D mid_value = domains[mid];
                if (value == mid_value)
                {
                    idx1 = mid;
                    idx2 = mid + 1;
                    break;
                }
                else if (value > mid_value)
                {
                    start = mid;
                }
                else
                {
                    end = mid;
                }
            }
        }
    }

    D val1 = domains[idx1];
    D val2 = domains[idx2];
    D t = (value - val1) / (val2 - val1);
    if (val2 == val1 || isnan(t))
    {
        t = D(0.5);
    }
    if (UseClamp)
    {
        t = clamp(t, D(0.0), D(1.0));
    }
    return Interp.interp(ranges[idx1], ranges[idx2], t);
}
