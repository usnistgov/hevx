#version 460 core
#extension GL_NV_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_gpu_shader_int64 : require

#include "prd.glsl"

layout(push_constant) uniform PushConstants {
  vec4 padding0;
  float iTime;
  float iTimeDelta;
  float iFrameRate;
  float iFrame;
  vec3 padding1;
  bool bDebugNormals;
  vec4 EyePosition;
  mat4 ModelMatrix;
  mat4 ModelViewMatrix;
  mat3 NormalMatrix;
};

layout(location = 0) rayPayloadInNV PerRayData prd;

void main() {
  prd.event = EVENT_RAY_MISSED;
}

