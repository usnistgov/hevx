// The MIT License
//
// Copyright (c) 2016-2017 Mohamad Moneimne and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#version 460 core

layout(binding = 0) uniform MatricesBuffer {
  mat4 ModelMatrix;
  mat4 NormalMatrix;
  mat4 ViewMatrix;
  mat4 ViewMatrixInverse;
  mat4 ModelViewMatrix;
  mat4 ModelViewMatrixInverse;
  mat4 ProjectionMatrix;
  mat4 ProjectionMatrixInverse;
  mat4 ModelViewProjectionMatrix;
  mat4 ModelViewProjectionMatrixInverse;
};

layout(location = 0) in vec4 Vertex;
#ifdef HAS_NORMALS
layout(location = 1) in vec3 Normal;
#endif
#ifdef HAS_TANGENTS
layout(location = 2) in vec4 Tangent;
#endif
#ifdef HAS_TEXCOORDS
layout(location = 3) in vec2 Texcoord;
#endif

layout(location = 0) out vec4 Po; // surface position in object-space
layout(location = 1) out vec4 Pe; // surface position in eye-space

layout(location = 2) out vec4 Eo; // eye position in object-space
layout(location = 3) out vec4 Ee; // eye position in eye-space

layout(location = 4) out vec3 Vo; // view vector in object-space
layout(location = 5) out vec3 Ve; // view vector in eye-space

#ifdef HAS_NORMALS
layout(location = 6) out vec3 No; // normal vector in object-space

#ifndef HAS_TANGENTS
layout(location = 7) out vec3 Ne; // normal vector in eye-space
#else // HAS_TANGENTS defined
layout(location = 7) out mat3 TBN;
#endif

#endif // HAS_NORMALS

layout(location = 10) out vec2 UV;

out gl_PerVertex {
  vec4 gl_Position;
};

void main() {
  Po = Vertex;
  Pe = ModelViewMatrix * Po;

  Ee = -ProjectionMatrixInverse[2];
  Eo = ModelViewMatrixInverse * Ee;

  Vo = normalize(Eo.xyz*Po.w-Po.xyz*Eo.w);
  Ve = normalize(Ee.xyz*Pe.w-Pe.xyz*Ee.w);

#ifdef HAS_NORMALS
  No = normalize(Normal);

#ifndef HAS_TANGENTS
  Ne = normalize(vec3(ModelMatrix * vec4(Normal.xyz, 0.0)));
#else

  vec3 normalW = normalize(vec3(NormalMatrix * vec4(Normal.xyz, 0.0)));
  vec3 tangentW = normalize(vec3(ModelMatrix * vec4(Tangent.xyz, 0.0)));
  vec3 bitangentW = cross(normalW, tangentW) * Tangent.w;
  TBN = mat3(tangentW, bitangentW, normalW);
#endif

#endif // HAS_NORMALS

#ifdef HAS_TEXCOORDS
  UV = Texcoord;
#else
  UV = vec2(0.0, 0.0);
#endif

  gl_Position = ModelViewProjectionMatrix * Vertex;
}
