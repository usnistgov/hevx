#version 460 core
#extension GL_NV_ray_tracing : require

layout(set = 0, binding = 0) uniform accelerationStructureNV topLevelAS;
layout(set = 0, binding = 1, rgba8) uniform image2D image;

layout(set = 0, binding = 2) uniform Matrices {
  mat4 model;
  mat4 view;
  mat4 proj;
  mat4 modelIT;
  mat4 viewInverse;
  mat4 projInverse;
} matrices;

layout(location = 0) rayPayloadNV vec3 hitValue;

void main() {
  const vec2 pixelCenter = vec2(gl_LaunchIDNV.xy) + vec2(0.5);
  const vec2 uv = pixelCenter / vec2(gl_LaunchSizeNV.xy);
  vec2 d = uv * 2.0 - 1.0;

  vec4 origin = matrices.viewInverse * vec4(0, 0, 0, 1);
  vec4 target = matrices.projInverse * vec4(d.x, d.y, 1, 1);
  vec4 direction = matrices.viewInverse * vec4(normalize(target.xyz), 0);

  uint rayFlags = gl_RayFlagsOpaqueNV;
  uint cullMask = 0xFF;
  float tmin = 0.001;
  float tmax = 10000.0;

  traceNV(topLevelAS, rayFlags, cullMask, 0 /*sbtRecordOffset*/,
    0 /*sbtRecordStride*/, 0 /*missIndex*/, origin.xyz, tmin, direction.xyz,
    tmax, 0 /*payload*/);
  imageStore(image, ivec2(gl_LaunchIDNV.xy), vec4(hitValue, 0.0));
}
