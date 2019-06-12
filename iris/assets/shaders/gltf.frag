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
//
// This fragment shader defines a reference implementation for Physically Based Shading of
// a microfacet surface material defined by a glTF model.
//
// References:
// [1] Real Shading in Unreal Engine 4
//     http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
// [2] Physically Based Shading at Disney
//     http://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf
// [3] README.md - Environment Maps
//     https://github.com/KhronosGroup/glTF-WebGL-PBR/#environment-maps
// [4] "An Inexpensive BRDF Model for Physically based Rendering" by Christophe Schlick
//     https://www.cs.virginia.edu/~jdl/bib/appearance/analytic%20models/schlick94b.pdf

//
// Additional changes made by Wesley Griffin <wesley.griffin@nist.gov>
// to adapt it to Vulkan GLSL and HEVx https://github.com/usnistgov/hevx
//

#version 460 core

#define MAX_LIGHTS 100

struct Light {
  vec4 direction;
  vec4 color;
};

layout(set = 0, binding = 1) uniform LightsBuffer {
  Light Lights[MAX_LIGHTS];
  int NumLights;
};

layout(set = 1, binding = 0) uniform MaterialBuffer {
  vec4 MetallicRoughnessNormalOcclusion;
  vec4 BaseColorFactor;
  vec3 EmissiveFactor;
};

#ifdef HAS_BASECOLOR_MAP
layout(set = 1, binding = 2) uniform sampler BaseColorSampler;
layout(set = 1, binding = 3) uniform texture2D BaseColorTexture;
#endif

#ifdef HAS_NORMAL_MAP
layout(set = 1, binding = 4) uniform sampler NormalSampler;
layout(set = 1, binding = 5) uniform texture2D NormalTexture;
#endif

#ifdef HAS_EMISSIVE_MAP
layout(set = 1, binding = 6) uniform sampler EmissiveSampler;
layout(set = 1, binding = 7) uniform texture2D EmissiveTexture;
#endif

#ifdef HAS_METALLICROUGHNESS_MAP
layout(set = 1, binding = 8) uniform sampler MetallicRoughnessSampler;
layout(set = 1, binding = 9) uniform texture2D MetallicRoughnessTexture;
#endif

#ifdef HAS_OCCLUSION_MAP
layout(set = 1, binding = 10) uniform sampler OcclusionSampler;
layout(set = 1, binding = 11) uniform texture2D OcclusionTexture;
#endif

layout(location = 0) in vec4 Po; // surface position in object-space
layout(location = 1) in vec4 Eo; // eye position in object-space
layout(location = 2) in vec3 Vo; // view vector in object-space
layout(location = 3) in vec3 No; // normal vector in object-space

layout(location = 4) in vec4 Pe; // surface position in eye-space
layout(location = 5) in vec4 Ee; // eye position in eye-space
layout(location = 6) in vec3 Ve; // view vector in eye-space
layout(location = 7) in vec3 Ne; // normal vector in eye-space

layout(location = 8) in vec2 UV;
#ifdef HAS_TEXCOORDS
layout(location = 9) in mat3 TBN;
#endif

layout(location = 0) out vec4 Color;

const float M_PI = 3.141592653589793;
const float MinRoughness = 0.04;

vec4 SRGBtoLINEAR(vec4 srgbIn) {
#ifdef MANUAL_SRGB
#ifdef SRGB_FAST_APPROXIMATION

  vec3 linOut = pow(srgbIn.xyz,vec3(2.2));

#else // SRGB_FAST_APPROXIMATION

  vec3 bLess = step(vec3(0.04045),srgbIn.xyz);
  vec3 linOut = mix(srgbIn.xyz/vec3(12.92), 
                    pow((srgbIn.xyz+vec3(0.055))/vec3(1.055),vec3(2.4)),
                    bLess);

#endif // SRGB_FAST_APPROXIMATION

  return vec4(linOut,srgbIn.w);;

#else // MANUAL_SRGB

  return srgbIn;

#endif // MANUAL_SRGB
}

// Find the normal for this fragment, pulling either from a predefined normal
// map or from the interpolated mesh normal and tangent attributes.
vec3 GetNormal() {
#ifdef HAS_TEXCOORDS

  // Retrieve the tangent space matrix
  mat3 tbn = TBN;

#else // HAS_TEXCOORDS

  vec3 ng = normalize(Ne);

  vec3 posDx = dFdx(Pe.xyz/ Pe.w);
  vec3 posDy = dFdy(Pe.xyz / Pe.w);
  vec3 texDx = dFdx(vec3(UV, 0.0));
  vec3 texDy = dFdy(vec3(UV, 0.0));

  vec3 t = (texDy.t * posDx - texDx.t * posDy)
           / (texDx.s * texDy.t - texDy.s * texDx.t);
  t = normalize(t - ng * dot(ng, t));
  vec3 b = normalize(cross(ng, t));

  mat3 tbn = mat3(t, b, ng);

#endif // HAS_TEXCOORDS

#ifdef HAS_NORMAL_MAP

  vec3 n = texture(sampler2D(NormalTexture, NormalSampler), UV0.st).rgb;
  vec3 s = vec3(MetallicRoughnessNormalOcclusion.zz, 1.0);
  n = normalize(tbn * ((2.0 * n - 1.0) * s));

#else // HAS_NORMAL_MAP

  // The tbn matrix is linearly interpolated, so we need to re-normalize
  vec3 n = normalize(tbn[2].xyz);

#endif // HAS_NORMAL_MAP

  return n;
}

void main() {
  vec3 n = GetNormal();
  //Color = vec4(n / vec3(2.f, 2.f, 2.f) + vec3(.5f, .5f, .5f), 1.f);
  //return;

  float metallic = MetallicRoughnessNormalOcclusion.x;
  float perceptualRoughness = MetallicRoughnessNormalOcclusion.y;

#ifdef HAS_METALLICROUGHNESS_MAP
  // Roughness is stored in the 'g' channel, metallic is stored in the 'b' channel.
  // This layout intentionally reserves the 'r' channel for (optional) occlusion map data
  vec4 mrSample = texture(sampler2D(MetallicRoughnessTexture, MetallicRoughnessSampler), UV0.st);
  perceptualRoughness = mrSample.g * perceptualRoughness;
  metallic = mrSample.b * metallic;
#endif

  perceptualRoughness = clamp(perceptualRoughness, MinRoughness, 1.0);
  metallic = clamp(metallic, 0.0, 1.0);
  // Roughness is authored as perceptual roughness; as is convention,
  // convert to material roughness by squaring the perceptual roughness [2].
  float alphaRoughness = perceptualRoughness * perceptualRoughness;

#ifdef HAS_BASECOLOR_MAP
  vec4 baseColor = SRGBtoLINEAR(texture(sampler2D(BaseColorTexture, BaseColorSampler), UV0.st)) * BaseColorFactor;
#else
  vec4 baseColor = BaseColorFactor;
#endif

  vec3 f0 = vec3(0.04);
  vec3 diffuseColor = baseColor.rgb * (vec3(1.0) - f0);
  diffuseColor *= 1.0 - metallic;
  vec3 specularColor = mix(f0, baseColor.rgb, metallic);

  // Compute reflectance.
  float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);

  // For typical incident reflectance range (between 4% to 100%) set the
  // grazing reflectance to 100% for typical fresnel effect.
  // For very low reflectance range on highly diffuse objects (below 4%),
  // incrementally reduce grazing reflecance to 0%.
  float reflectance90 = clamp(reflectance * 25.0, 0.0, 1.0);
  vec3 specularEnvironmentR0 = specularColor.rgb;
  vec3 specularEnvironmentR90 = vec3(1.0, 1.0, 1.0) * reflectance90;
  vec3 specularEnvironmentR90R0Delta = specularEnvironmentR90 - specularEnvironmentR0;

  vec3 v = normalize(Ve);
  float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);
  vec3 reflection = -normalize(reflect(v, n));

  float roughnessSq = alphaRoughness * alphaRoughness;
  vec3 color = vec3(0.2) * diffuseColor; // ambient

  for (int i = 0; i < NumLights; ++i) {
    if (Lights[i].color.a > 0) {
      vec3 l = normalize(Lights[i].direction.xyz);
      vec3 h = normalize(l + v);

      float NdotL = clamp(dot(n, l), 0.001, 1.0);
      float NdotH = clamp(dot(n, h), 0.0, 1.0);
      float LdotH = clamp(dot(l, h), 0.0, 1.0);
      float VdotH = clamp(dot(v, h), 0.0, 1.0);

      // Calculate the shading terms for the microfacet specular shading model

      vec3 F = specularEnvironmentR0 + specularEnvironmentR90R0Delta *
               pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);

      float attenuationL = 2.0 * NdotL /
        (NdotL + sqrt(roughnessSq + (1.0 - roughnessSq) * (NdotL * NdotL)));
      float attenuationV = 2.0 * NdotV /
        (NdotV + sqrt(roughnessSq + (1.0 - roughnessSq) * (NdotV * NdotV)));
      float G = attenuationL * attenuationV;

      float f = (NdotH * roughnessSq - NdotH) * NdotH + 1.0;
      float D = roughnessSq / (M_PI * f * f);

      // Calculation of analytical lighting contribution
      vec3 diffuseContrib = (1.0 - F) * (diffuseColor / M_PI);
      vec3 specContrib = F * G * D / (4.0 * NdotL * NdotV);

      // Obtain final intensity as reflectance (BRDF) scaled by the energy of the light (cosine law)
      color += NdotL * Lights[i].color.rgb * (diffuseContrib + specContrib);
    }
  }

  // Apply optional PBR terms for additional (optional) shading
#ifdef HAS_OCCLUSION_MAP
  float ao = texture(sampler2D(OcclusionTexture, OcclusionSampler), UV0.st).r;
  color = mix(color, color * ao,MetallicRoughnessNormalOcclusion.w);
#endif

#ifdef HAS_EMISSIVE_MAP
  vec3 emissive = SRGBtoLINEAR(texture(sampler2D(EmissiveTexture, EmissiveSampler), UV0.st)).rgb * EmissiveFactor;
  color += emissive;
#endif

  Color = vec4(pow(color, vec3(1.0/2.2)), baseColor.a);
}
