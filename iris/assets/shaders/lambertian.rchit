#version 460 core
#extension GL_NV_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "rand.glsl"
#include "prd.glsl"

#define MAX_LIGHTS 100

struct Light {
  vec4 direction;
  vec4 color;
};

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

layout(set = 0, binding = 0) uniform MatricesBuffer {
  mat4 ViewMatrix;
  mat4 ViewMatrixInverse;
  mat4 ProjectionMatrix;
  mat4 ProjectionMatrixInverse;
};

layout(set = 0, binding = 1) uniform LightsBuffer {
  Light Lights[MAX_LIGHTS];
  int NumLights;
};

struct Sphere {
  float aabbMinX;
  float aabbMinY;
  float aabbMinZ;
  float aabbMaxX;
  float aabbMaxY;
  float aabbMaxZ;
};

layout(set = 1, binding = 0) uniform accelerationStructureNV scene;
layout(set = 1, binding = 1, rgba8) uniform image2D image;
layout(std430, set = 1, binding = 2) readonly buffer SphereBuffer {
  Sphere spheres[];
};

layout(location = 0) rayPayloadInNV PerRayData prd;

hitAttributeNV vec3 Po; // Hit position in world-space
hitAttributeNV vec3 No; // Normal in world-space

void main() {
  const vec3 target = Po + No + rand_vec3_in_unit_sphere(prd.rngState);

  prd.scatterEvent = SCATTER_EVENT_RAY_BOUNCED;
  prd.scatterOrigin = Po;
  prd.scatterDirection = (target - Po);
  prd.attenuation = vec3(.5f, .5f, .5f);
}
