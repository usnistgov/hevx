#version 460 core
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

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
  float padding2;
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

layout(location = 0) rayPayloadInNV vec4 hitValue;
hitAttributeNV vec3 normalVector;

void main() {
  const vec3 P = hitValue.xyz;
  const vec3 N = normalize((ViewMatrixInverse * vec4(normalVector, 0.f)).xyz);
  hitValue = vec4(.5f) * vec4(N + vec3(1.f), 2.f);
}

