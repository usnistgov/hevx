float drand48(inout uint64_t state) {
  const uint64_t a = 0x5DEECE66DUL;
  const uint64_t c = 0xBUL;
  const uint64_t mask = 0xFFFFFFFFFFFFUL;
  state = a * state + c;
  return float(state & mask) / float(mask+1UL);
}

vec3 rand_vec3_in_unit_sphere(inout uint64_t state) {
  vec3 p;
  do {
    const float x = drand48(state);
    const float y = drand48(state);
    const float z = drand48(state);
    p = 2.f * vec3(x, y, z) - vec3(1.f, 1.f, 1.f);
  } while (dot(p, p) >= 1.f);
  return p;
}
