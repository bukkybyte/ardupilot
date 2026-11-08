#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <functional>
#include "smartpack_proto.h"
#include "smartpack_key.h"
#include "smartpack_pack.h"
#include <AP_HAL/AP_HAL.h>

uint32_t sim_now = 1000;
bool sim_armed = false;
std::function<void()> sim_thread;

// ---- simulated Nano pack at I2C address 0x0B, running the real pack-side code --------
struct SimPack : AP_HAL::I2CDevice {
    SmartPackState st;
    uint8_t status = SMARTPACK_ST_IDLE;
    uint8_t response[8] = {0};
    bool busy_pending = false;
    int uses_counted = 0;

    bool read_registers(uint8_t reg, uint8_t *buf, uint32_t len) override {
        memset(buf, 0, len);
        if (reg == SMARTPACK_REG_STATUS) {
            if (busy_pending) {                       // pack "loop()" finishes hashing
                uint8_t info[16];
                smartpack_build_info(st.rec, info);
                smartpack_compute_response(SMARTPACK_KEY, st.nonce, info, response);
                status = SMARTPACK_ST_READY;
                busy_pending = false;
                buf[0] = SMARTPACK_ST_BUSY;           // first poll still sees BUSY
            } else {
                buf[0] = status;
            }
        } else if (reg == SMARTPACK_REG_RESPONSE) {
            memcpy(buf, response, len < 8 ? len : 8);
        } else if (reg == SMARTPACK_REG_INFO) {
            uint8_t info[16];
            smartpack_build_info(st.rec, info);
            memcpy(buf, info, len < 16 ? len : 16);
        }
        return true;
    }
    bool transfer(const uint8_t *send, uint32_t n, uint8_t *, uint32_t) override {
        if (send[0] == SMARTPACK_REG_NONCE && n == 9) {
            memcpy(st.nonce, send + 1, 8);
            st.nonce_valid = true;
            status = SMARTPACK_ST_BUSY;
            busy_pending = true;
        } else if (send[0] == SMARTPACK_REG_USE && n == 9) {
            SmartPackState snap = st;
            if (smartpack_use_check(SMARTPACK_KEY, snap, send + 1) && smartpack_use_commit(st, snap)) {
                uses_counted++;
                printf("   [t=%5.1fs] NANO: use counted, count now %u\n", ((int)sim_now - 1000) / 1000.0, (unsigned)st.rec.count);
            } else {
                printf("   [t=%5.1fs] NANO: use report rejected\n", ((int)sim_now - 1000) / 1000.0);
            }
        }
        return true;
    }
};
static SimPack pack;

struct DeadDev : AP_HAL::I2CDevice {      // nothing at this address
    bool read_registers(uint8_t, uint8_t *, uint32_t) override { return false; }
    bool transfer(const uint8_t *, uint32_t, uint8_t *, uint32_t) override { return false; }
};
static DeadDev dead;

AP_HAL::OwnPtr<AP_HAL::I2CDevice> AP_HAL::I2CDeviceManager::get_device(uint8_t, uint8_t addr, uint32_t, bool, uint32_t)
{
    return AP_HAL::OwnPtr<AP_HAL::I2CDevice>(addr == 0x0B ? (AP_HAL::I2CDevice *)&pack : (AP_HAL::I2CDevice *)&dead);
}

static AP_HAL::Util util_obj; static AP_HAL::Scheduler sched_obj; static AP_HAL::I2CDeviceManager mgr_obj;
static AP_HAL::HAL hal_obj = { &util_obj, &sched_obj, &mgr_obj };
const AP_HAL::HAL &hal = hal_obj;

// ---- scenario, driven by the fake clock ------------------------------------------------
struct SimDone {};
static void flight(uint32_t t, uint32_t arm_at, uint32_t disarm_at, int &st_, const char *name)
{
    if (st_ == 0 && t >= arm_at)    { sim_armed = true;  st_ = 1; printf("   [t=%5.1fs] --- ARM (%s)\n", t / 1000.0, name); }
    if (st_ == 1 && t >= disarm_at) { sim_armed = false; st_ = 2; printf("   [t=%5.1fs] --- DISARM\n", t / 1000.0); }
}
static int f1 = 0, f2 = 0;
void sim_delay(uint16_t ms)
{
    sim_now += ms;
    const uint32_t t = sim_now - 1000;
    flight(t, 10000, 15000, f1, "armed for 5 s");
    flight(t, 25000, 65000, f2, "armed for 40 s");
    if (t >= 80000) throw SimDone();
}

#define private public
#define protected public
#include "AP_PackAuth.cpp"
#undef private
#undef protected

int main()
{
    srand(7);
    pack.st.rec = {12345, 100, 20261031UL, 2, 500};
    AP_PackAuth pka;
    pka._bus.v = -1; pka._addr.v = 11; pka._num.v = 1;
    pka.init();
    printf("AP_PACKAUTH_MIN_FLIGHT_S = %d\n", AP_PACKAUTH_MIN_FLIGHT_S);
    printf("pack starts with count %u\n", (unsigned)pack.st.rec.count);
    try { sim_thread(); } catch (SimDone &) {}
    char msg[64];
    bool gate = pka.gate_check(msg, sizeof(msg));
    printf("\nend: pack count = %u, uses counted = %d, gate %s%s%s\n", (unsigned)pack.st.rec.count,
           pack.uses_counted, gate ? "OPEN" : "closed: ", gate ? "" : msg, "");
    return 0;
}
