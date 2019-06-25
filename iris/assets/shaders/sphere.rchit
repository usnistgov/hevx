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

layout(location = 0) rayPayloadInNV vec4 hitValue;

hitAttributeNV vec3 No; // Normal in world-space

void main() {
  const vec4 Po = vec4(hitValue.xyz, 1.f);
  const vec4 Pe = ModelViewMatrix * Po;

  if (bDebugNormals) {
    // HEV is -Z up, so surfaces with upward normals have high Z component.
    // Most other engines use +Y up, so flip the components here to visualize it.
    const vec3 n = normalize(vec3(No.x, -No.z, -No.y));
    hitValue = vec4(vec3(.5f) * (n + vec3(1.f)), 1.f);
    return;
  }

  const vec3 l = normalize(Lights[0].direction.xyz * Pe.w - Lights[0].direction.w * Pe.xyz);
  const float dP = max(dot(l, normalize(No)), .2f);
  hitValue = vec4(dP, dP, dP, 1.f);
}

