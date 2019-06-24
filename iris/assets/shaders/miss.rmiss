#version 460 core
#extension GL_NV_ray_tracing : require

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

layout(location = 0) rayPayloadInNV vec4 hitValue;

void main() {
  const vec3 direction = normalize(gl_WorldRayDirectionNV);
  const float t = .5f * (-direction.z + 1.f);
  hitValue = vec4(mix(vec3(1.f, 1.f, 1.f), vec3(.5f, .7f, 1.f), t), 1.f);
  //hitValue = vec4(0.f, 0.f, 0.f, 1.f);
}

