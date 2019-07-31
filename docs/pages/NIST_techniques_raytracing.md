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

### Extending Materials

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
                    "type": XXX,
                    "uri": "raygen.glsl"
                },
                {
                    "type": YYY,
                    "uri": "closesetHit.glsl"
                },
                {
                    "type": ZZZ,
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

## Known Implementations

- TODO

## Resources

- TODO