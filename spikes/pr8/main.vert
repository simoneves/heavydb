#version 460
import scale;
layout(location = 0) in float in_x;
layout(std430, binding = 0) uniform SCALE_UBO { float d0; float d1; float r0; float r1; };
void main() {
  float x = scaleValue<LogTransform>(in_x, d0, d1, r0, r1);
  gl_Position = vec4(x, 0.0, 0.0, 1.0);
}
