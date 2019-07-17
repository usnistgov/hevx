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

#ifdef HAS_COLORS
layout(location = 8) out vec4 C; // color
#endif

layout(location = 0) out vec4 Color;

void main() {
  Color = vec4(pow(C.rgb, vec3(1.0/2.2)), C.a);
}
