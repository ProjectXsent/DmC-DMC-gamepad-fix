#!/usr/bin/env python3
"""Inspect which input APIs a Windows executable (or DLL) uses. Stdlib only.

Usage:
  inspect_imports.py GAME.exe                 # summarise PE imports + bitness
  inspect_imports.py --dumpbin dump.txt       # same, from `dumpbin /imports` text
  inspect_imports.py --exports XINPUT.dll     # list a DLL's exports (name + ordinal)
  inspect_imports.py --exports OUR.dll --expect-ordinals 2,3   # CI check (exit 1 on miss)
  inspect_imports.py --exports OUR.dll --expect-names DirectInput8Create

Imports of interest are flagged so you can tell which DLL a shim must proxy.
"""
import argparse
import re
import struct
import sys

INTERESTING = ("xinput", "dinput", "winmm", "hid", "steam_api", "sdl", "xlive", "openxr")
MACHINES = {0x14C: "x86 (32-bit)", 0x8664: "x64 (64-bit)", 0xAA64: "ARM64"}


class PE:
    def __init__(self, data):
        self.d = data
        if data[:2] != b"MZ":
            raise ValueError("not a PE file (missing MZ)")
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("not a PE file (missing PE signature)")
        (self.machine, nsec, _, _, _, optsz, _) = struct.unpack_from("<HHIIIHH", data, pe + 4)
        opt = pe + 24
        magic = struct.unpack_from("<H", data, opt)[0]
        self.is64 = magic == 0x20B
        if magic not in (0x10B, 0x20B):
            raise ValueError("unknown optional header magic %#x" % magic)
        dd = opt + (112 if self.is64 else 96)
        self.dirs = [struct.unpack_from("<II", data, dd + 8 * i) for i in range(16)]
        sec = opt + optsz
        self.sections = []
        for i in range(nsec):
            _, vsize, va, rawsize, rawptr = struct.unpack_from("<8sIIII", data, sec + 40 * i)
            self.sections.append((va, max(vsize, rawsize), rawptr))

    def off(self, rva):
        for va, size, raw in self.sections:
            if va <= rva < va + size:
                return raw + (rva - va)
        raise ValueError("RVA %#x not in any section" % rva)

    def cstr(self, rva):
        o = self.off(rva)
        return self.d[o:self.d.index(b"\0", o)].decode("latin-1")

    def imports(self):
        rva, _ = self.dirs[1]
        res = {}
        if not rva:
            return res
        o = self.off(rva)
        step = 8 if self.is64 else 4
        flag = 1 << (63 if self.is64 else 31)
        fmt = "<Q" if self.is64 else "<I"
        while True:
            oft, _, _, name, ft = struct.unpack_from("<IIIII", self.d, o)
            if not name and not ft:
                break
            dll = self.cstr(name)
            t = self.off(oft or ft)
            funcs = []
            while True:
                v = struct.unpack_from(fmt, self.d, t)[0]
                if v == 0:
                    break
                if v & flag:
                    funcs.append(("ordinal", v & 0xFFFF))
                else:
                    funcs.append(("name", self.cstr((v & 0x7FFFFFFF) + 2)))
                t += step
            res[dll] = funcs
            o += 20
        return res

    def exports(self):
        rva, _ = self.dirs[0]
        if not rva:
            return []
        o = self.off(rva)
        (_, _, _, _, _, base, nfunc, nname, aof, aon, aoo) = struct.unpack_from("<IIHHIIIIIII", self.d, o)
        names = {}
        for i in range(nname):
            nrva = struct.unpack_from("<I", self.d, self.off(aon) + 4 * i)[0]
            idx = struct.unpack_from("<H", self.d, self.off(aoo) + 2 * i)[0]
            names[idx] = self.cstr(nrva)
        out = []
        for i in range(nfunc):
            if struct.unpack_from("<I", self.d, self.off(aof) + 4 * i)[0]:
                out.append((base + i, names.get(i)))
        return out


def parse_dumpbin(text):
    """Parse `dumpbin /imports` output into {dll: [(kind, value), ...]}."""
    res, cur = {}, None
    for line in text.replace("\r", "").split("\n"):
        m = re.match(r"^    (\S+\.(?:dll|DLL|exe|drv|ocx))\s*$", line)
        if m:
            cur = m.group(1)
            res[cur] = []
            continue
        if cur is None:
            continue
        m = re.match(r"^\s+Ordinal\s+(\d+)\s*$", line)
        if m:
            res[cur].append(("ordinal", int(m.group(1))))
            continue
        m = re.match(r"^\s+[0-9A-Fa-f]+\s+(\S+)\s*$", line)
        if m and not re.search(r"(Address Table|Name Table|stamp|forwarder)", line):
            res[cur].append(("name", m.group(1)))
    return res


def report_imports(imports, machine=None):
    if machine:
        print("Architecture :", machine)
    print("Imported DLLs: %d" % len(imports))
    print("\nInput-related imports:")
    found = False
    for dll, funcs in sorted(imports.items(), key=lambda kv: kv[0].lower()):
        if any(k in dll.lower() for k in INTERESTING):
            found = True
            print("  %s" % dll)
            for kind, v in funcs:
                print("     %s %s" % ("ordinal" if kind == "ordinal" else "name   ", v))
    if not found:
        print("  (none)")
    xin = [d for d in imports if d.lower().startswith("xinput")]
    print("\nVerdict:")
    if xin:
        by_ord = any(k == "ordinal" for d in xin for k, _ in imports[d])
        print("  Proxy %s%s" % (xin[0], " -- imported BY ORDINAL, so the proxy's .def must export ordinals" if by_ord else ""))
    else:
        print("  No XInput import found; check DirectInput/WinMM/HID instead.")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", nargs="?", help="PE file to inspect")
    ap.add_argument("--dumpbin", metavar="TXT", help="parse a `dumpbin /imports` text dump instead")
    ap.add_argument("--exports", metavar="DLL", help="list exports of a DLL")
    ap.add_argument("--expect-ordinals", help="comma-separated ordinals that --exports must contain")
    ap.add_argument("--expect-names", help="comma-separated export names that --exports must contain")
    a = ap.parse_args()

    if a.exports:
        pe = PE(open(a.exports, "rb").read())
        ex = pe.exports()
        print("%s  [%s]" % (a.exports, MACHINES.get(pe.machine, hex(pe.machine))))
        for ordv, name in ex:
            print("  %4d  %s" % (ordv, name or "(noname)"))
        if a.expect_ordinals:
            want = {int(x) for x in a.expect_ordinals.split(",") if x.strip()}
            have = {o for o, _ in ex}
            missing = sorted(want - have)
            if missing:
                print("MISSING ordinals: %s" % missing, file=sys.stderr)
                return 1
            print("OK: ordinals %s present" % sorted(want))
        if a.expect_names:
            wantn = {x.strip() for x in a.expect_names.split(",") if x.strip()}
            missing_n = sorted(wantn - {n for _, n in ex if n})
            if missing_n:
                print("MISSING names: %s" % missing_n, file=sys.stderr)
                return 1
            print("OK: names %s present" % sorted(wantn))
        return 0

    if a.dumpbin:
        report_imports(parse_dumpbin(open(a.dumpbin, encoding="latin-1").read()))
        return 0

    if a.path:
        pe = PE(open(a.path, "rb").read())
        report_imports(pe.imports(), MACHINES.get(pe.machine, hex(pe.machine)))
        return 0

    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
