#version 460 core
#extension GL_NV_ray_tracing : require

layout(push_constant) uniform PushConstants {
  vec4 iMouse;
  float iTime;
  float iTimeDelta;
  float iFrameRate;
  float iFrame;
  vec3 iResolution;
  float padding0;
  vec4 EyePosition;
  mat4 ModelMatrix;
  mat4 ModelViewMatrix;
  mat3 NormalMatrix;
};

layout(location = 0) rayPayloadInNV vec3 hitValue;

void main() {
  hitValue = vec3(0.f, 0.f, 0.f);
}

