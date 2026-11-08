#pragma once
#include <stdio.h>
extern uint32_t sim_now;
enum MAV_SEVERITY { MAV_SEVERITY_INFO };
#define GCS_SEND_TEXT(sev, fmt, args...) do { (void)sev; printf("   [t=%5.1fs] " fmt "\n", ((int)sim_now - 1000) / 1000.0, ##args); } while (0)
