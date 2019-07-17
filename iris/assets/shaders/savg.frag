#version 460 core

#define MAX_LIGHTS 100

struct Light {
  vec4 direction;
  vec4 color;
};

layout(push_constant) uniform PushConstants {
  vec4 iMouse;
  float iTime;
  float iTimeDelta;
  float iFrameRate;
  float iFrame;
  vec3 iResolution;
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

layout(location = 0) in vec4 Po; // surface position in object-space
layout(location = 1) in vec4 Eo; // eye position in object-space
layout(location = 2) in vec3 Vo; // view vector in object-space
#ifdef HAS_NORMALS
layout(location = 3) in vec3 No; // normal vector in object-space
#endif

layout(location = 4) in vec4 Pe; // surface position in eye-space
layout(location = 5) in vec4 Ee; // eye position in eye-space
layout(location = 6) in vec3 Ve; // view vector in eye-space
#ifdef HAS_NORMALS
layout(location = 7) in vec3 Ne; // normal vector in eye-space
#endif

layout(location = 8) in vec4 C; // color

layout(location = 0) out vec4 Color;

const float M_PI = 3.141592653589793;

void main() {
#ifdef HAS_NORMALS
  vec3 n = normalize(Ne);
  vec3 v = normalize(Ve);
  float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);

  vec3 color = vec3(0.2) * C.rgb; // ambient

  for (int i = 0; i < NumLights; ++i) {
    if (Lights[i].color.a > 0) {
      vec3 l = normalize(Lights[i].direction.xyz * Pe.w - Lights[i].direction.w * Pe.xyz);
      vec3 h = normalize(l + v);

      float NdotL = clamp(dot(n, l), 0.001, 1.0);
      float NdotH = clamp(dot(n, h), 0.0, 1.0);
      float LdotH = clamp(dot(l, h), 0.0, 1.0);
      float VdotH = clamp(dot(v, h), 0.0, 1.0);

      color += NdotL * Lights[i].color.rgb * (C.rgb / M_PI + pow(NdotH, 2.f));
    }
  }

  Color = vec4(pow(color, vec3(1.0/2.2)), C.a);
#else
  Color = vec4(pow(C.rgb, vec3(1.0/2.2)), C.a);
#endif
}
