#pragma once

#include "AP_PackAuth_config.h"

#if AP_PACKAUTH_ENABLED

#include <AP_Common/AP_Common.h>
#include <AP_HAL/AP_HAL.h>
#include <AP_HAL/I2CDevice.h>
#include <AP_Param/AP_Param.h>

class AP_PackAuth
{
public:
    AP_PackAuth();
    CLASS_NO_COPY(AP_PackAuth);

    // start the authenticator thread (idempotent, call after parameters are loaded)
    void init();

    // arming gate: true only if every required pack has a fresh, valid proof
    // (and, if enabled, the pack count is correctly signed for this autopilot);
    // otherwise fills buf with the reason
    bool gate_check(char *buf, size_t len) const;

#if AP_PACKAUTH_PARAMS_ENABLED
    static const struct AP_Param::GroupInfo var_info[];
#endif

private:
    struct Pack {
        AP_HAL::OwnPtr<AP_HAL::I2CDevice> dev;
        volatile bool ok = false;            // written by the thread only
        uint32_t ok_ms = 0;
        uint32_t last_scan_ms = 0;
        uint32_t last_try_ms = 0;
        uint16_t ok_serial = 0;
        int32_t  serial = -1;
        int8_t   bus_found = -1;
        uint8_t  last_status = 0xFF;
        const char *why = "starting";
        uint16_t n_sent = 0, n_ok = 0, n_bad = 0, n_xfer_fail = 0, n_timeout = 0, n_scans = 0;
    };

    void thread();
    void step(uint8_t i);
    void find_device(uint8_t i);
    bool probe(uint8_t bus, uint8_t addr);
    void challenge(uint8_t i);
    bool alive() const;
    uint8_t num_packs() const;
    uint8_t addr_for(uint8_t i) const { return (uint8_t)(cfg_addr() + i); }
    bool config_ok(uint8_t n) const { return ((unsigned)cfg_addr() + n - 1) <= 0x77; }

#if AP_PACKAUTH_CFG_SIGNED
    // true if ARMING_PKA_NUM carries a valid signature for this autopilot
    bool cfg_valid() const;
    // unprovisioned (or tampered) count: probe every slot and report what was found
    bool detect_mode() const { return !cfg_valid(); }
    void report_detect();
    uint32_t _detect_ms = 0;
    AP_Int16 _s0, _s1, _s2, _s3;
#endif

#if AP_PACKAUTH_DEBUG
    void debug_print();
    uint32_t _dbg_ms = 0;
    uint32_t _scan_mask = 0;
#endif

#if AP_PACKAUTH_PARAMS_ENABLED
    AP_Int8 _bus;
    AP_Int8 _addr;
    AP_Int8 _num;
    int8_t  cfg_bus() const  { return _bus; }
    uint8_t cfg_addr() const { return (uint8_t)_addr.get(); }
#else
    int8_t  cfg_bus() const  { return AP_PACKAUTH_BUS_DEFAULT; }
    uint8_t cfg_addr() const { return AP_PACKAUTH_ADDR_DEFAULT; }
#endif

    Pack _pack[AP_PACKAUTH_MAX_PACKS];

    bool _started = false;
    bool _thread_failed = false;
    volatile uint32_t _beat_ms = 0;          // thread heartbeat
};

#endif  // AP_PACKAUTH_ENABLED