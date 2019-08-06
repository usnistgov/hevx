# NIST_techniques_raytracing

## Contributors

- Wesley Griffin, NIST, wesley.griffin@nist.gov

## Status

Draft

## Dependencies

Written against the glTF 2.0 spec.

## Overview

TODO

## glTF Schema Updates

### Extending Primitives

This extension deifnes two new attribute semantic properties for primitives:

| Name    | Accessor Type | Component Type | Description               |
|---------|---------------|----------------|---------------------------|
| `_AABB` | `"VEC3"`      | `5126` (FLOAT) | Axis Aligned Bounding Box |

The Axis Aligned Bounding Box is a sequential set of two `VEC3` values defining
the minimum and maximum corners of the AABB respectively.

### Extending Materials

~~~
"materials": {
    {
        "extensions": {
            "NIST_techniques_raytracing": {
                "technique": 0
            }
        }
    }
}
~~~

### Extension

NIST_techniques_raytracing is defined in the asset's top level `extensions`
property with the following additional values.

~~~
{
    "extensions": {
        "NIST_techniques_raytracing": {
            "programs": [
                {
                    "raygenShader": 0,
                    "closestHitShader": 2,
                    "missShader": 3,
                }
            ],
            "shaders": [
                {
                    "type": 256,
                    "uri": "raygen.glsl"
                },
                {
                    "type": 1024,
                    "uri": "closesetHit.glsl"
                },
                {
                    "type": 2048,
                    "uri": "miss.glsl"
                }
            ],
            "techniques": [
                {
                    "program": 0,
                }
            ]
        }
    }
}
~~~

### JSON Schema

| Name    | Accessor Type | Component Type | Description               |
|---------|---------------|----------------|---------------------------|
| `_AABB` | `"VEC3"`      | `5126` (FLOAT) | Axis Aligned Bounding Box |

#### shader.type

- **Type:** `integer`
- **Required:** Yes
- **Allowed values:**
  - `256` VK_SHADER_STAGE_RAYGEN_BIT_NV
  - `512` VK_SHADER_STAGE_ANY_HIT_BIT_NV
  - `1024` VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV
  - `2048` VK_SHADER_STAGE_MISS_BIT_NV
  - `4096` VK_SHADER_STAGE_INTERSECTION_BIT_NV
  - `8192` VK_SHADER_STAGE_CALLABLE_BIT_NV

## Known Implementations

- TODO

## Resources

- TODO