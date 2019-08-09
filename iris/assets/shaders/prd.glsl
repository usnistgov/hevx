struct Ray {
  vec3 origin;
  vec3 direction;
};

#define EVENT_RAY_BOUNCED 0
#define EVENT_RAY_KILLED 1
#define EVENT_RAY_MISSED 2

struct PerRayData {
  uint64_t rngState;
  int event; // EVENT_*
  Ray scattered;
  vec3 attenuation;
};

