"""Execute the supplied Android ELF command builders offline; compare shipped EXE vectors.

No Android process, phone, or proprietary tracking engine is emulated here.
Only the small packet serialization routines are run in Unicorn.
"""
import os
import hashlib
import json
import pathlib
import sys

ROOT = pathlib.Path(os.environ.get('XREAL_ANDROID_PROJECT',str(pathlib.Path(__file__).resolve().parents[2])))
sys.path.insert(0, str(ROOT / 'ServerAppResearch' / 'tools'))
sys.path.insert(0, str(ROOT / 'ServerAppResearch' / 'tools' / 'pydeps'))
from decode_native_strings import Decoder
from unicorn.arm64_const import (
    UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3,
    UC_ARM64_REG_X4, UC_ARM64_REG_X5, UC_ARM64_REG_X6,
    UC_ARM64_REG_SP, UC_ARM64_REG_LR,
)

LIB = ROOT / 'ServerAppResearch/decompiled/resources/lib/arm64-v8a/libnr_glasses_api.so'
OUT = pathlib.Path(sys.argv[1]) if len(sys.argv)>1 else pathlib.Path(__file__).resolve().parents[1] / 'dist/XREAL-StableScreen-v0.1.1'
decoder = Decoder(str(LIB), 0)
u = decoder.u
vectors = json.loads((OUT / 'self-test.json').read_text())['packets']
cases = [
    ('imu_on', True, 0x19, b'\1'),
    ('imu_off', True, 0x19, b'\0'),
    ('config_length', True, 0x14, b''),
    ('config_next', True, 0x15, b''),
    ('mode_query', False, 7, b''),
    ('stereo60', False, 8, b'\3'),
]
results = []
for name, imu, cmd, payload in cases:
    data, output, length = 0x60000000, 0x60010000, 0x60020000
    u.mem_write(data, payload or b'\0')
    u.mem_write(output, b'\0' * 128)
    u.mem_write(length, b'\0' * 8)
    args = [0, cmd, data, len(payload), output, length] if imu else [0, cmd, 0x1337, data, len(payload), output, length]
    for reg, val in zip([UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2,
                         UC_ARM64_REG_X3, UC_ARM64_REG_X4, UC_ARM64_REG_X5,
                         UC_ARM64_REG_X6], args):
        u.reg_write(reg, val)
    u.reg_write(UC_ARM64_REG_SP, 0x701ff000)
    u.reg_write(UC_ARM64_REG_LR, 0x70200000)
    u.emu_start(0xa92e0 if imu else 0xa91dc, 0x70200000, count=100000)
    raw = bytes(u.mem_read(output, 64))
    results.append({'name': name, 'match': raw.hex() == vectors[name],
                    'androidPacket': raw.hex(), 'windowsPacket': vectors[name]})
report = {'library': str(LIB.relative_to(ROOT)),
          'librarySha256': hashlib.sha256(LIB.read_bytes()).hexdigest(),
          'functions': {'cmd_build_imu': '0xa92e0', 'cmd_build_sdk': '0xa91dc'},
          'tests': results, 'passed': all(r['match'] for r in results),
          'scope': 'Packet serialization only; live USB responses and tracking remain unverified'}
(OUT / 'android-protocol-comparison.json').write_text(json.dumps(report, indent=2), encoding='utf8')
for r in results:
    print(r['name'], 'MATCH' if r['match'] else 'MISMATCH')
raise SystemExit(0 if report['passed'] else 1)
