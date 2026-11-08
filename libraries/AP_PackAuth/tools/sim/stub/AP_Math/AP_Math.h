#pragma once
#include <stdint.h>
inline int16_t constrain_int16(int16_t a, int16_t lo, int16_t hi) { return a < lo ? lo : (a > hi ? hi : a); }
inline uint16_t get_random16() { return 4; }
