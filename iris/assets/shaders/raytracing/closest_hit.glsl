#version 460 core
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) rayPayloadInNV vec3 hitValue;
hitAttributeNV vec3 normalVector;

void main() {
  vec3 N = normalize(normalVector.xyz);
  vec3 L = normalize(vec3(5, 4, 3));
  float LdotN = max(dot(L, N), 0.2);
  hitValue = vec3(LdotN);
}
