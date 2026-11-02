module scale;

public interface ITransform { public static float apply(float v); }
public struct LogTransform : ITransform { public static float apply(float v) { return log(v); } }
public struct PassThru : ITransform { public static float apply(float v) { return v; } }

public float scaleValue<T : ITransform>(float v, float d0, float d1, float r0, float r1) {
  float t = (T.apply(v) - T.apply(d0)) / (T.apply(d1) - T.apply(d0));
  return lerp(r0, r1, t);
}
