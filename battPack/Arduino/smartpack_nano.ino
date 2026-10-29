// SmartPack test slave for Arduino Nano (ATmega328P)
// Pretends to be a 4S smart battery on I2C address 0x0B:
//   - standard SBS telemetry registers (read by ArduPilot's SMBus code)
//   - custom challenge/response registers 0x70..0x72 (see smartpack_proto.h)
//
// Wiring (through a 3.3V<->5V level shifter, see notes):
//   Nano A4 = SDA, A5 = SCL, GND common with the flight controller.
//
// Serial monitor (115200, newline) commands:
//   v <mV>   set pack voltage      e.g. v 14800
//   i <mA>   set current (negative = discharging)   e.g. i -1500
//   t        toggle "tamper" (sends a wrong response -> FC must refuse to arm)
//   p        print state

#include <Wire.h>
#include "smartpack_proto.h"
#include "smartpack_key.h"

static const uint8_t  I2C_ADDR   = 0x0B;
static const uint16_t PACK_SERIAL = 0x1234;

// SBS registers used by ArduPilot's generic SMBus backend
static const uint8_t REG_TEMP      = 0x08;  // 0.1 K
static const uint8_t REG_VOLTAGE   = 0x09;  // mV
static const uint8_t REG_CURRENT   = 0x0A;  // mA, signed, negative = discharge
static const uint8_t REG_REMAIN    = 0x0F;  // mAh
static const uint8_t REG_FULLCAP   = 0x10;  // mAh
static const uint8_t REG_CYCLES    = 0x17;
static const uint8_t REG_SPECINFO  = 0x1A;
static const uint8_t REG_SERIAL    = 0x1C;
static const uint8_t REG_CELL_LAST = 0x3C;  // cell4..cell1 = 0x3C..0x3F
static const uint8_t REG_CELL_FIRST = 0x34; // 12-cell range 0x34..0x3F
static const uint8_t NUM_CELLS = 4;

// simulated pack state
static volatile uint16_t g_voltage_mv = 14800;
static volatile int16_t  g_current_ma = -1500;
static volatile bool     g_tamper = false;

// protocol state (shared with the I2C interrupt handlers)
static volatile uint8_t g_reg = 0;
static volatile uint8_t g_status = SMARTPACK_ST_IDLE;
static volatile bool    g_nonce_pending = false;
static uint8_t g_nonce[SMARTPACK_NONCE_LEN];
static uint8_t g_response[SMARTPACK_RESPONSE_LEN];

static volatile uint16_t g_rx = 0, g_req = 0;   // near the other globals


static void put16(uint8_t *b, uint16_t v)
{
    b[0] = v & 0xFF;
    b[1] = v >> 8;
}

void printHex(const uint8_t *b, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) { if (b[i] < 16) Serial.print('0'); Serial.print(b[i], HEX); }
    Serial.println();
}

// master wrote something: [reg] or [reg, data...]
static void onReceiveHandler(int n)
{
    if (n < 1) {
        return;
    }
    const uint8_t reg = Wire.read();
    g_reg = reg;
    n--;

    if (reg == SMARTPACK_REG_NONCE && n == SMARTPACK_NONCE_LEN) {
        for (uint8_t i = 0; i < SMARTPACK_NONCE_LEN; i++) {
            g_nonce[i] = Wire.read();
        }
        g_status = SMARTPACK_ST_BUSY;   // never clock-stretch: tell the FC to poll
        g_nonce_pending = true;
        return;
    }
    while (n-- > 0) {
        Wire.read();                    // ignore anything else
    }
}

// master wants to read the register selected by the last write
static void onRequestHandler()
{
    uint8_t buf[SMARTPACK_RESPONSE_LEN] = {0};
    uint8_t len = 2;
    const uint8_t reg = g_reg;
    static uint32_t t = 0;

    if (millis() - t > 2000) {
        t = millis();
        Serial.print(F("rx=")); Serial.print(g_rx);
        Serial.print(F(" req=")); Serial.print(g_req);
        Serial.print(F(" reg=0x")); Serial.print(g_reg, HEX);
        Serial.print(F(" status=")); Serial.println(g_status);
    }

    if (reg == SMARTPACK_REG_STATUS) {
        buf[0] = g_status;
        len = 1;
    } else if (reg == SMARTPACK_REG_RESPONSE) {
        if (g_status == SMARTPACK_ST_READY) {
            memcpy(buf, g_response, SMARTPACK_RESPONSE_LEN);
        }
        len = SMARTPACK_RESPONSE_LEN;
    } else {
        switch (reg) {
        case REG_TEMP:     put16(buf, 2982); break;   // 25.0 C
        case REG_VOLTAGE:  put16(buf, g_voltage_mv); break;
        case REG_CURRENT:  put16(buf, (uint16_t)g_current_ma); break;
        case REG_REMAIN:   put16(buf, 4000); break;
        case REG_FULLCAP:  put16(buf, 5000); break;
        case REG_CYCLES:   put16(buf, 12); break;
        case REG_SPECINFO: put16(buf, 0x0021); break; // SBS v2 -> tells ArduPilot: no PEC
        case REG_SERIAL:   put16(buf, PACK_SERIAL); break;
        default:
            if (reg >= REG_CELL_FIRST && reg <= 0x3F) {
                // cell1 = 0x3F, cell2 = 0x3E ... ; unused cells report 0
                const uint8_t cell = 0x3F - reg;          // 0-based
                if (cell < NUM_CELLS) {
                    put16(buf, g_voltage_mv / NUM_CELLS);
                }
            }
            break;                                          // unknown register -> zeros
        }
    }
    Wire.write(buf, len);
}

static void handleSerial()
{
    if (!Serial.available()) {
        return;
    }
    const char c = Serial.read();
    if (c == 'v') {
        const uint16_t v = (uint16_t)Serial.parseInt();
        noInterrupts(); g_voltage_mv = v; interrupts();
    } else if (c == 'i') {
        const int16_t i = (int16_t)Serial.parseInt();
        noInterrupts(); g_current_ma = i; interrupts();
    } else if (c == 't') {
        g_tamper = !g_tamper;
    } else if (c != 'p') {
        return;
    }
    Serial.print(F("V=")); Serial.print(g_voltage_mv);
    Serial.print(F("mV I=")); Serial.print(g_current_ma);
    Serial.print(F("mA tamper=")); Serial.println(g_tamper ? F("ON") : F("off"));
}

void setup()
{
    Serial.begin(115200);
    pinMode(A5, OUTPUT); digitalWrite(A5, HIGH);

    // boot self-test: compare with the host test vector
    // (nonce 00..07, serial 0x1234 -> 25CADA811DA5C784 with the dev key)
    uint8_t n[SMARTPACK_NONCE_LEN] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint8_t r[SMARTPACK_RESPONSE_LEN];
    smartpack_compute_response(SMARTPACK_KEY, n, PACK_SERIAL, r);
    Serial.print(F("self-test response: "));
    for (uint8_t i = 0; i < SMARTPACK_RESPONSE_LEN; i++) {
        if (r[i] < 16) Serial.print('0');
        Serial.print(r[i], HEX);
    }
    Serial.println();

    Wire.begin(I2C_ADDR);
    Wire.onReceive(onReceiveHandler);
    Wire.onRequest(onRequestHandler);
    Serial.println(F("SmartPack slave ready on 0x0B"));
}

void loop()
{
    if (g_nonce_pending) {
        uint8_t nonce[SMARTPACK_NONCE_LEN];
        noInterrupts();
        memcpy(nonce, g_nonce, sizeof(nonce));
        g_nonce_pending = false;
        interrupts();

        uint8_t resp[SMARTPACK_RESPONSE_LEN];
        smartpack_compute_response(SMARTPACK_KEY, nonce, PACK_SERIAL, resp);
        Serial.print(F("nonce rx ")); printHex(nonce, 8);
        Serial.print(F("resp tx  ")); printHex(resp, 8);
        if (g_tamper) {
            resp[0] ^= 0x01;
        }
        noInterrupts();
        memcpy(g_response, resp, sizeof(resp));
        if (!g_nonce_pending) {
            g_status = SMARTPACK_ST_READY;
        }
        interrupts();
    }
    handleSerial();
}
