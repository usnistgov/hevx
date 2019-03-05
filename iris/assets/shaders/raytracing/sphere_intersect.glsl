#version 460 core
#extension GL_NV_ray_tracing : require

layout(set = 0, binding = 2) uniform Matrices {
  mat4 model;
  mat4 view;
  mat4 proj;
  mat4 modelIT;
  mat4 viewInverse;
  mat4 projInverse;
} matrices;

hitAttributeNV vec3 normalVector;

void main() {
  vec3 center = vec3(0, 0, 0);
  float radius = 2.0;

  vec3 oc = gl_WorldRayOriginNV - center;
  float a = dot(gl_WorldRayDirectionNV, gl_WorldRayDirectionNV);
  float b = dot(oc, gl_WorldRayDirectionNV);
  float c = dot(oc, oc) - (radius * radius);
  float d = b * b - a * c;

  if (d > 0.0) {
    float t1 = (-b - sqrt(d)) / a;
    float t2 = (-b + sqrt(d)) / a;

    if (gl_RayTminNV < t1 && t1 < gl_RayTmaxNV) {
      vec3 hitValue = gl_WorldRayOriginNV + gl_WorldRayDirectionNV * t1;
      normalVector = (hitValue - center) / radius;
      reportIntersectionNV(t1, 0);
    } else if (gl_RayTminNV < t2 && t2 < gl_RayTmaxNV) {
      vec3 hitValue = gl_WorldRayOriginNV + gl_WorldRayDirectionNV * t2;
      normalVector = (hitValue - center) / radius;
      reportIntersectionNV(t2, 0);
    }
  }
}
