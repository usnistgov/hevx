These are support files for continuous integration testing of HEVx.

The CI testing runs on [Travis CI](https://travis-ci.org/usnistgov/hevx) and
uses Docker images for the build platform matrix.

The `Dockerfile.*` files in this directory are used to build the docker images.

The Dockerfile files use the `build-vulkan-sdk.py` to build the VulkanSDK.

The `hevx-ci.sh` and `create-report.py` files are used for nightly builds
outside of Travis CI.