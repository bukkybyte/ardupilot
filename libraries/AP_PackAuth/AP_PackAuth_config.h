#pragma once

#include <AP_HAL/AP_HAL_Boards.h>

#ifndef AP_PACKAUTH_ENABLED
#define AP_PACKAUTH_ENABLED 0          // enabled per board in hwdef.dat
#endif

#ifndef AP_PACKAUTH_PARAMS_ENABLED
#define AP_PACKAUTH_PARAMS_ENABLED 1   // 0 = bus/address/count fixed at build time, no parameters
#endif

#ifndef AP_PACKAUTH_BUS_DEFAULT
#define AP_PACKAUTH_BUS_DEFAULT -1     // -1 = scan all I2C buses for each pack
#endif

#ifndef AP_PACKAUTH_ADDR_DEFAULT
#define AP_PACKAUTH_ADDR_DEFAULT 0x0B  // pack N is at ADDR + N - 1
#endif

#ifndef AP_PACKAUTH_MAX_PACKS
#define AP_PACKAUTH_MAX_PACKS 5
#endif

#ifndef AP_PACKAUTH_NUM_MIN
#define AP_PACKAUTH_NUM_MIN 1          // firmware floor: the parameter can't go below this
#endif

#ifndef AP_PACKAUTH_NUM_DEFAULT
#define AP_PACKAUTH_NUM_DEFAULT 1
#endif

#ifndef AP_PACKAUTH_CFG_SIGNED
#define AP_PACKAUTH_CFG_SIGNED 0       // 1 = the pack count must carry a valid signature for this Cube
#endif

#if AP_PACKAUTH_CFG_SIGNED && !AP_PACKAUTH_PARAMS_ENABLED
#error "AP_PACKAUTH_CFG_SIGNED needs AP_PACKAUTH_PARAMS_ENABLED"
#endif

#ifndef AP_PACKAUTH_DEBUG
#define AP_PACKAUTH_DEBUG 0            // testing only: status text every 5 s
#endif