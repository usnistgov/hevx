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

layout(set = 1, binding = 0) uniform accelerationStructureNV scene;
layout(set = 1, binding = 1, rgba8) uniform image2D image;

struct Sphere {
  float aabbMinX;
  float aabbMinY;
  float aabbMinZ;
  float aabbMaxX;
  float aabbMaxY;
  float aabbMaxZ;
};

layout(std430, set = 1, binding = 2) readonly buffer SphereBuffer {
  Sphere spheres[];
};

layout(location = 0) rayPayloadInNV vec3 hitValue;
hitAttributeNV vec3 normalVector;

void main() {
  const vec4 P = vec4(hitValue, 1.f);

  vec3 Ve = normalize(EyePosition.xyz*P.w - P.xyz*EyePosition.w);
  vec3 v = normalize(Ve);
  const vec3 n = normalize((ViewMatrixInverse * vec4(normalVector, 0.f)).xyz);

  hitValue = vec3(.2f); // ambient

  for (int i = 0; i < NumLights; ++i) {
    if (Lights[i].color.a > 0) {
      vec3 l = normalize(Lights[i].direction.xyz);
      vec3 h = normalize(l + v);

      float NdotL = clamp(dot(n, l), 0.001, 1.0);
      float NdotH = clamp(dot(n, h), 0.0, 1.0);
      float LdotH = clamp(dot(l, h), 0.0, 1.0);
      float VdotH = clamp(dot(v, h), 0.0, 1.0);

      hitValue += NdotL * Lights[i].color.rgb * vec3(.6f, .6f, .6f);
    }
  }

  //hitValue = vec4(.5f) * vec4(n + vec3(1.f), 1.f);
}

