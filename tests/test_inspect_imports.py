"""Tests for tools/inspect_imports.py using a synthetic in-memory PE32 image."""
import importlib.util
import os
import struct
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("inspect_imports", os.path.join(HERE, "..", "tools", "inspect_imports.py"))
ii = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ii)

SEC_VA, SEC_RAW, SEC_SIZE = 0x1000, 0x200, 0x400


def build_pe():
    img = bytearray(SEC_RAW + SEC_SIZE)
    img[0:2] = b"MZ"
    struct.pack_into("<I", img, 0x3C, 0x40)
    img[0x40:0x44] = b"PE\0\0"
    # COFF: machine=i386, 1 section, ..., SizeOfOptionalHeader=224
    struct.pack_into("<HHIIIHH", img, 0x44, 0x14C, 1, 0, 0, 0, 224, 0x2102)
    opt = 0x58
    struct.pack_into("<H", img, opt, 0x10B)  # PE32
    dd = opt + 96
    struct.pack_into("<II", img, dd + 0, SEC_VA + 0x000, 0x100)   # export dir
    struct.pack_into("<II", img, dd + 8, SEC_VA + 0x200, 40)      # import dir
    sec = opt + 224
    struct.pack_into("<8sIIII", img, sec, b".rdata", SEC_SIZE, SEC_VA, SEC_SIZE, SEC_RAW)

    def put(rva, data):
        img[SEC_RAW + (rva - SEC_VA): SEC_RAW + (rva - SEC_VA) + len(data)] = data

    # --- exports: ordinals 2 and 3 (Base=2) named XInputGetState / XInputSetState
    put(SEC_VA + 0x000, struct.pack("<IIHHIIIIIII", 0, 0, 0, 0, SEC_VA + 0x30, 2, 2, 2,
                                    SEC_VA + 0x40, SEC_VA + 0x50, SEC_VA + 0x60))
    put(SEC_VA + 0x30, b"fake.dll\0")
    put(SEC_VA + 0x40, struct.pack("<II", SEC_VA + 0x100, SEC_VA + 0x110))
    put(SEC_VA + 0x50, struct.pack("<II", SEC_VA + 0x70, SEC_VA + 0x80))
    put(SEC_VA + 0x60, struct.pack("<HH", 0, 1))
    put(SEC_VA + 0x70, b"XInputGetState\0")
    put(SEC_VA + 0x80, b"XInputSetState\0")
    # --- imports: XINPUT1_3.dll by ordinal 2, plus one named function
    put(SEC_VA + 0x200, struct.pack("<IIIII", SEC_VA + 0x240, 0, 0, SEC_VA + 0x280, SEC_VA + 0x240))
    put(SEC_VA + 0x240, struct.pack("<III", 0x80000002, SEC_VA + 0x260, 0))
    put(SEC_VA + 0x260, b"\0\0" + b"foo\0")
    put(SEC_VA + 0x280, b"XINPUT1_3.dll\0")
    return bytes(img)


class InspectTests(unittest.TestCase):
    def setUp(self):
        self.pe = ii.PE(build_pe())

    def test_machine(self):
        self.assertEqual(self.pe.machine, 0x14C)
        self.assertFalse(self.pe.is64)

    def test_imports(self):
        imp = self.pe.imports()
        self.assertEqual(list(imp), ["XINPUT1_3.dll"])
        self.assertEqual(imp["XINPUT1_3.dll"][0], ("ordinal", 2))

    def test_exports(self):
        self.assertEqual(self.pe.exports(), [(2, "XInputGetState"), (3, "XInputSetState")])

    def test_dumpbin_parser(self):
        text = ("  Section contains the following imports:\r\n\r\n    XINPUT1_3.dll\r\n"
                "               2321C8C Import Address Table\r\n\r\n"
                "                      Ordinal     2\r\n                      Ordinal     3\r\n\r\n"
                "    WINMM.dll\r\n\r\n                    0 timeGetTime\r\n")
        r = ii.parse_dumpbin(text)
        self.assertEqual(r["XINPUT1_3.dll"], [("ordinal", 2), ("ordinal", 3)])
        self.assertEqual(r["WINMM.dll"], [("name", "timeGetTime")])

    def test_cli_expect_names(self):
        import subprocess, sys, tempfile
        with tempfile.NamedTemporaryFile(suffix=".dll", delete=False) as f:
            f.write(build_pe())
        script = os.path.join(HERE, "..", "tools", "inspect_imports.py")
        ok = subprocess.run([sys.executable, script, "--exports", f.name, "--expect-names", "XInputGetState"],
                            capture_output=True)
        bad = subprocess.run([sys.executable, script, "--exports", f.name, "--expect-names", "DirectInput8Create"],
                             capture_output=True)
        os.unlink(f.name)
        self.assertEqual(ok.returncode, 0)
        self.assertEqual(bad.returncode, 1)

    def test_rejects_non_pe(self):
        with self.assertRaises(ValueError):
            ii.PE(b"not a pe file at all" * 10)


if __name__ == "__main__":
    unittest.main()
