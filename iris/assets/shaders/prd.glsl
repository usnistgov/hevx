#define SCATTER_EVENT_RAY_BOUNCED 0
#define SCATTER_EVENT_RAY_KILLED 1
#define SCATTER_EVENT_RAY_MISSED 2

struct PerRayData {
  uint64_t rngState;
  int scatterEvent; // SCATTER_EVENT_*
  vec3 scatterOrigin;
  vec3 scatterDirection;
  vec3 attenuation;
};

