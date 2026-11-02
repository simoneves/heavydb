// PR 8 spike: the quantitative scale as an ordinary function, for the one combination
// the fastSymbol stage uses (double domain, float range, two values, pass-through
// transform, null check, default interpolation), to separate the cost of generics
// from the cost of a module.
module quantitativeScaleConcrete;

public float evalQuantitativeScaleConcrete(
    double domains[2], double null_domain, float ranges[2], float null_range,
    double domain_value)
{
    if (domain_value == null_domain)
    {
        return null_range;
    }
    double t = (domain_value - domains[0]) / (domains[1] - domains[0]);
    if (domains[1] == domains[0] || isnan(t))
    {
        t = 0.5;
    }
    return lerp(ranges[0], ranges[1], float(t));
}
