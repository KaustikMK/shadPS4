#!/usr/bin/env python3
"""
Simple PS4 firmware installer for shadPS4.

This script extracts modules from a `PS4UPDATE.PUP` file and copies them
into shadPS4's `sys_modules` directory. Only files recognised in the PUP
are extracted. Compressed entries are inflated using zlib.

Usage:
    python3 install_ps4_firmware.py /path/to/PS4UPDATE.PUP
"""

# SPDX-License-Identifier: GPL-2.0-or-later

import io
import os
import shutil
import struct
import sys
import zlib
from pathlib import Path

FILES = {
    0x1: "emc_ipl.slb",
    0x2: "eap_kbl.slb",
    0x3: "torus2_fw.slb",
    0x4: "sam_ipl.slb",
    0x5: "coreos.slb",
    0x6: "system_exfat.img",
    0x7: "eap_kernel.slb",
    0x8: "eap_vsh_fat16.img",
    0x9: "preinst_fat32.img",
    0xB: "preinst2_fat32.img",
    0xC: "system_ex_exfat.img",
    0xD: "emc_ipl.slb",
    0xE: "eap_kbl.slb",
    0x20: "emc_ipl.slb",
    0x21: "eap_kbl.slb",
    0x22: "torus2_fw.slb",
    0x23: "sam_ipl.slb",
    0x24: "emc_ipl.slb",
    0x25: "eap_kbl.slb",
    0x26: "sam_ipl.slb",
    0x27: "sam_ipl.slb",
    0x28: "emc_ipl.slb",
    0x2A: "emc_ipl.slb",
    0x2B: "eap_kbl.slb",
    0x2C: "emc_ipl.slb",
    0x2D: "sam_ipl.slb",
    0x2E: "emc_ipl.slb",
    0x30: "torus2_fw.bin",
    0x31: "sam_ipl.slb",
    0x32: "sam_ipl.slb",
    0x101: "eula.xml",
    0x200: "orbis_swu.elf",
    0x202: "orbis_swu.self",
    0xD01: "bd_firm.slb",
    0xD02: "sata_bridge_fw.slb",
    0xD09: "cp_fw_kernel.slb",
}

PUP_HEADER_FORMAT = "<IBBBBBBHHHIIHHI"
PUP_ENTRY_FORMAT = "<QQQQ"

class PUPEntry:
    def __init__(self, flags, offset, file_size, memory_size):
        self.flags = flags
        self.offset = offset
        self.file_size = file_size
        self.memory_size = memory_size
        self.data = b""

    @property
    def file_name(self):
        return FILES.get(self.flags >> 20, f"unknown_{self.flags >> 20:04X}")

    @property
    def compressed(self):
        return bool(self.flags & 0x8)

    def process_bytes(self, data: bytes):
        if self.compressed:
            decompress = zlib.decompressobj()
            inflated = decompress.decompress(data)
            inflated += decompress.flush()
            self.data = inflated
        else:
            self.data = data

class PUP:
    def __init__(self, path: Path):
        self.path = path
        self.entries = []

    def parse(self):
        with self.path.open('rb') as f:
            data = f.read()
        stream = io.BytesIO(data)
        header = stream.read(struct.calcsize(PUP_HEADER_FORMAT))
        (magic, version, mode, endian, flags, content, product, padding,
         header_size, hash_size, file_size, padding2, entries_count,
         flags2, unk1C) = struct.unpack(PUP_HEADER_FORMAT, header)
        if magic != 0x1D3D154F:
            raise RuntimeError('Invalid PUP file')
        for _ in range(entries_count):
            entry_data = stream.read(struct.calcsize(PUP_ENTRY_FORMAT))
            entry = PUPEntry(*struct.unpack(PUP_ENTRY_FORMAT, entry_data))
            self.entries.append(entry)
        for entry in self.entries:
            stream.seek(entry.offset)
            entry.process_bytes(stream.read(entry.file_size))

    def extract(self, out_dir: Path):
        out_dir.mkdir(parents=True, exist_ok=True)
        for entry in self.entries:
            if not entry.file_name:
                continue
            out_path = out_dir / entry.file_name
            with out_path.open('wb') as f:
                f.write(entry.data)


def main():
    if len(sys.argv) != 2:
        print("Usage: install_ps4_firmware.py PS4UPDATE.PUP")
        sys.exit(1)

    pup_path = Path(sys.argv[1])
    if not pup_path.exists():
        print(f"PUP file '{pup_path}' not found")
        sys.exit(1)

    sys_modules = Path('sys_modules')
    try:
        pup = PUP(pup_path)
        pup.parse()
        pup.extract(sys_modules)
        print(f"Firmware extracted to {sys_modules.resolve()}")
    except Exception as e:
        print(f"Failed to install firmware: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()
