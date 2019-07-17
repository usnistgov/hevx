#version 460 core

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

layout(location = 0) in vec3 Vertex;
#ifdef HAS_NORMALS
layout(location = 1) in vec3 Normal;
#endif
#ifdef HAS_COLORS
layout(location = 2) in vec4 Color;
#endif

layout(location = 0) out vec4 Po; // surface position in object-space
layout(location = 1) out vec4 Eo; // eye position in object-space
layout(location = 2) out vec3 Vo; // view vector in object-space
#ifdef HAS_NORMALS
layout(location = 3) out vec3 No; // normal vector in object-space
#endif

layout(location = 4) out vec4 Pe; // surface position in eye-space
layout(location = 5) out vec4 Ee; // eye position in eye-space
layout(location = 6) out vec3 Ve; // view vector in eye-space
#ifdef HAS_NORMALS
layout(location = 7) out vec3 Ne; // normal vector in eye-space
#endif

#ifdef HAS_COLORS
layout(location = 8) out vec4 C; // color
#endif

out gl_PerVertex {
  vec4 gl_Position;
};

void main() {
  Po = vec4(Vertex, 1.0);
  Pe = ModelViewMatrix * Po;

  Eo = EyePosition;
  Ee = ModelViewMatrix * Eo;

  Vo = normalize(Eo.xyz*Po.w - Po.xyz*Eo.w);
  Ve = normalize(Ee.xyz*Pe.w - Pe.xyz*Ee.w);

#ifdef HAS_NORMALS
  No = normalize(Normal);
  Ne = NormalMatrix * No;
#endif

#ifdef HAS_COLORS
  C = Color;
#endif

  gl_Position = ProjectionMatrix * Pe;
}
