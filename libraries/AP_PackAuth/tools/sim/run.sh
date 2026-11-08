#!/usr/bin/env bash
# Run the REAL AP_PackAuth.cpp against a simulated pack with a fake clock (no hardware).
# Two arm/disarm flights (5 s and 40 s) are replayed with the default and with a 0 s minimum.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB="$HERE/../.."
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
cp "$LIB/AP_PackAuth.cpp" "$LIB/AP_PackAuth.h" "$LIB/AP_PackAuth_config.h" "$LIB/smartpack_proto.h" "$W"
if [ -f "$LIB/smartpack_key.h" ]; then cp "$LIB/smartpack_key.h" "$W/smartpack_key.h"; else cp "$LIB/smartpack_key.h.example" "$W/smartpack_key.h"; fi
cp "$LIB/nano/smartpack_nano/smartpack_pack.h" "$HERE/sim_test.cpp" "$W"
for s in 30 0; do
    echo "=== AP_PACKAUTH_MIN_FLIGHT_S=$s"
    g++ -std=gnu++11 -I"$HERE/stub" -I"$W" -DAP_PACKAUTH_ENABLED=1 -DAP_PACKAUTH_MIN_FLIGHT_S=$s \
        "$W/sim_test.cpp" -o "$W/sim$s"
    "$W/sim$s"
done
