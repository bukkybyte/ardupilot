#pragma once
#include <AP_Common/AP_Common.h>
extern uint32_t sim_now;
extern bool sim_armed;
void sim_delay(uint16_t ms);
extern std::function<void()> sim_thread;
namespace AP_HAL {
    inline uint32_t millis() { return sim_now; }
    class Semaphore {};
    class I2CDevice {
    public:
        virtual ~I2CDevice() {}
        Semaphore *get_semaphore() { return &sem; }
        virtual bool read_registers(uint8_t reg, uint8_t *buf, uint32_t len) = 0;
        virtual bool transfer(const uint8_t *send, uint32_t send_len, uint8_t *recv, uint32_t recv_len) = 0;
        void set_retries(uint8_t) {}
        Semaphore sem;
    };
    template<class T> class OwnPtr {
    public:
        OwnPtr() {}
        OwnPtr(T *p_) : p(p_) {}
        T *operator->() { return p; }
        bool operator!() const { return p == nullptr; }
        explicit operator bool() const { return p != nullptr; }
        T *p = nullptr;
    };
    class Util { public:
        int snprintf(char *str, size_t size, const char *fmt, ...) {
            va_list ap; va_start(ap, fmt); int r = vsnprintf(str, size, fmt, ap); va_end(ap); return r; }
        bool get_random_vals(uint8_t *b, size_t n) { for (size_t i = 0; i < n; i++) b[i] = (uint8_t)rand(); return true; }
        bool get_soft_armed() { return sim_armed; }
        bool get_system_id_unformatted(uint8_t buf[], uint8_t &len) { for (uint8_t i = 0; i < 12; i++) buf[i] = i; len = 12; return true; }
    };
    class Scheduler { public:
        enum priority_base { PRIORITY_IO };
        bool thread_create(std::function<void()> f, const char *, uint32_t, priority_base, int8_t) { sim_thread = f; return true; }
        void delay(uint16_t ms) { sim_delay(ms); }
        bool is_system_initialized() { return true; }
    };
    class I2CDeviceManager { public:
        OwnPtr<I2CDevice> get_device(uint8_t, uint8_t, uint32_t = 400000, bool = false, uint32_t = 4);
        uint32_t get_bus_mask() const { return 3; }
    };
    class HAL { public: Util *util; Scheduler *scheduler; I2CDeviceManager *i2c_mgr; };
}
#define WITH_SEMAPHORE(sem) (void)(sem)
