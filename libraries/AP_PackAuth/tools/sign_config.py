#!/usr/bin/env python3
"""Sign the pack-count config for ONE Cube so ARMING_PKA_NUM can't be changed
without the key.

  sign_config.py --key libraries/AP_PackAuth/smartpack_key.h --uid <24 hex chars> --packs 3 > file_name

--uid is the "PackAuth cfg-id" the Cube prints while unprovisioned.
Prints a Mission Planner param file (name,value) to stdout.
"""
import argparse, re, struct, sys

M = (1 << 64) - 1
def rotl(x, b): return ((x << b) | (x >> (64 - b))) & M

def siphash24(key, data):
    k0, k1 = struct.unpack('<QQ', key)
    v = [0x736f6d6570736575 ^ k0, 0x646f72616e646f6d ^ k1,
         0x6c7967656e657261 ^ k0, 0x7465646279746573 ^ k1]
    def rnd():
        v0, v1, v2, v3 = v
        v0 = (v0 + v1) & M; v1 = rotl(v1, 13); v1 ^= v0; v0 = rotl(v0, 32)
        v2 = (v2 + v3) & M; v3 = rotl(v3, 16); v3 ^= v2
        v0 = (v0 + v3) & M; v3 = rotl(v3, 21); v3 ^= v0
        v2 = (v2 + v1) & M; v1 = rotl(v1, 17); v1 ^= v2; v2 = rotl(v2, 32)
        v[:] = [v0, v1, v2, v3]
    n = len(data) - len(data) % 8
    for i in range(0, n, 8):
        m = struct.unpack('<Q', data[i:i + 8])[0]
        v[3] ^= m; rnd(); rnd(); v[0] ^= m
    b = (len(data) & 0xFF) << 56
    for i, c in enumerate(data[n:]):
        b |= c << (8 * i)
    v[3] ^= b; rnd(); rnd(); v[0] ^= b
    v[2] ^= 0xFF
    for _ in range(4): rnd()
    return (v[0] ^ v[1] ^ v[2] ^ v[3]) & M

def load_key(path):
    hexes = re.findall(r'0x([0-9a-fA-F]{2})', open(path).read())
    if len(hexes) < 16:
        sys.exit("could not read 16 key bytes from " + path)
    return bytes(int(h, 16) for h in hexes[:16])

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--key', required=True)
    ap.add_argument('--uid', required=True, help='24 hex chars')
    ap.add_argument('--packs', type=int, required=True)
    a = ap.parse_args()
    uid = bytes.fromhex(a.uid)
    if len(uid) != 12: sys.exit("uid must be 12 bytes (24 hex chars)")
    if not 1 <= a.packs <= 5: sys.exit("packs must be 1..5")
    mac = siphash24(load_key(a.key), bytes([0xC1, a.packs]) + uid)
    print("ARMING_PKA_NUM,%d" % a.packs)
    for i in range(4):
        w = (mac >> (16 * i)) & 0xFFFF
        print("ARMING_PKA_S%d,%d" % (i, w - 0x10000 if w >= 0x8000 else w))

if __name__ == '__main__':
    main()