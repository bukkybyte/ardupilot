#pragma once
#include <AP_Common/AP_Common.h>
class AP_Param { public:
    struct GroupInfo { const char *name; uint8_t idx; size_t sz; };
    static void setup_object_defaults(const void *, const GroupInfo *) {}
};
template<class T> class AP_ParamT { public: T get() const { return v; } operator T() const { return v; } T v = 0; };
typedef AP_ParamT<int8_t> AP_Int8;
typedef AP_ParamT<int16_t> AP_Int16;
#define AP_GROUPINFO(name, idx, clazz, element, def) { name, idx, sizeof(((clazz*)nullptr)->element) }
#define AP_GROUPEND { nullptr, 0, 0 }
