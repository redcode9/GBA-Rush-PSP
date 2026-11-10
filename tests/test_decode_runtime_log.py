"""Tests for tools/decode_runtime_log.py on hand-built .gbrlog files.

The fixtures pack the header and records the way runtime_log.c does; the
payload CRC and the header CRC (over the header bytes before it) are the
last 8 header bytes: 56 and 60 up to version 5, 88 and 92 from version 6.
"""

import binascii
import contextlib
import importlib.util
import io
import re
import struct
import unittest
from pathlib import Path


# tools/ is not a package, so load the script as a module by path.
MODULE_PATH = Path(__file__).parents[1] / "tools" / "decode_runtime_log.py"
SPEC = importlib.util.spec_from_file_location("decode_runtime_log", MODULE_PATH)
decoder = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(decoder)


def fixture() -> bytes:
    """Version 1 log with one main-ring link-state record."""
    record = struct.pack(
        ">QIIHBBIII", 110, 7, 1, 8, 1, 0, 6, 0, 0
    )
    header = bytearray(64)
    struct.pack_into(
        ">4sHHHHHHIIIIIIQQ", header, 0, b"GBRL", 1, 64, 32, 0, 1, 0,
        0x12345678, 2, 1, 0x06060110, 333, 0x42505249, 100, 120,
    )
    struct.pack_into(">I", header, 56, binascii.crc32(record) & 0xFFFFFFFF)
    struct.pack_into(">I", header, 60, binascii.crc32(header[:60]) & 0xFFFFFFFF)
    return bytes(header) + record


def fixture_v2() -> bytes:
    """Version 2 log with one main and one critical record."""
    main = struct.pack(
        ">QIIHBBIII", 110, 7, 1, 8, 1, 0, 6, 0, 0
    )
    critical = struct.pack(
        ">QIIHBBIII", 115, 8, 1, 20, 3, 0, 13, 0, 0
    )
    payload = main + critical
    header = bytearray(64)
    struct.pack_into(
        ">4sHHHHHHIIIIIIQQ", header, 0, b"GBRL", 2, 64, 32, 1, 1, 0,
        0x12345678, 8, 0, 0x06060110, 333, 0x42505249, 100, 120,
    )
    struct.pack_into(">I", header, 56, binascii.crc32(payload) & 0xFFFFFFFF)
    struct.pack_into(">I", header, 60, binascii.crc32(header[:60]) & 0xFFFFFFFF)
    return bytes(header) + payload


def fixture_v3() -> bytes:
    """Version 3 log of link events from the retired link design."""
    records = [
        struct.pack(
            ">QIIHBBIII", 110, 7, 1, 22, 3, 0,
            4, 3, 0xFFFFFFF9,
        ),
        struct.pack(
            ">QIIHBBIII", 111, 7, 2, 22, 3, 1,
            4, 18, 0x80410709,
        ),
        struct.pack(
            ">QIIHBBIII", 112, 7, 3, 24, 3, 0,
            4, 2, 2,
        ),
        struct.pack(
            ">QIIHBBIII", 113, 7, 4, 23, 3, 4,
            1234, 87, 0,
        ),
    ]
    payload = b"".join(records)
    header = bytearray(64)
    struct.pack_into(
        ">4sHHHHHHIIIIIIQQ", header, 0, b"GBRL", 3, 64, 32,
        len(records), 0, 0, 0x12345678, 2, 1, 0x06060110, 333,
        0x42505249, 100, 120,
    )
    struct.pack_into(">I", header, 56, binascii.crc32(payload) & 0xFFFFFFFF)
    struct.pack_into(">I", header, 60, binascii.crc32(header[:60]) & 0xFFFFFFFF)
    return bytes(header) + payload


def fixture_v6(build_id: bytes = b"0123456789ab-dirty",
               record: bytes = b"") -> bytes:
    """Version 6 log: the 96-byte header with a build id, one record (a
    log-flush unless given)."""
    if not record:
        record = struct.pack(
            ">QIIHBBIII", 110, 7, 1, 18, 1, 0, 2, 1, 0
        )
    header = bytearray(96)
    struct.pack_into(
        ">4sHHHHHHIIIIIIQQ", header, 0, b"GBRL", 6, 96, 32, 0, 1, 0,
        0x12345678, 2, 1, 0x06060110, 333, 0x42505249, 100, 120,
    )
    header[56:56 + len(build_id)] = build_id
    struct.pack_into(">I", header, 88, binascii.crc32(record) & 0xFFFFFFFF)
    struct.pack_into(">I", header, 92, binascii.crc32(header[:92]) & 0xFFFFFFFF)
    return bytes(header) + record


class RuntimeLogDecoderTests(unittest.TestCase):
    def test_decodes_header_and_record(self):
        snapshot = decoder.parse_snapshot(fixture())

        self.assertEqual(snapshot["session_id"], 0x12345678)
        self.assertEqual(snapshot["flush_reason"], "link-failure")
        self.assertEqual(snapshot["rom_identity"], "BPRI")
        self.assertEqual(snapshot["records"][0]["event"], "link-state")
        self.assertEqual(snapshot["records"][0]["elapsed_us"], 10)

    def test_rejects_corrupt_payload(self):
        damaged = bytearray(fixture())
        damaged[-1] ^= 1

        with self.assertRaisesRegex(ValueError, "payload checksum"):
            decoder.parse_snapshot(bytes(damaged))

    def test_decodes_v2_critical_records(self):
        snapshot = decoder.parse_snapshot(fixture_v2())

        self.assertEqual(snapshot["version"], 2)
        self.assertEqual(snapshot["critical_count"], 1)
        self.assertEqual(snapshot["records"][1]["source"], "critical")

    def test_older_link_records_are_shown_raw(self):
        snapshot = decoder.parse_snapshot(fixture_v3())
        description = decoder.describe(snapshot["records"][0])

        self.assertEqual(description, "flags=0x00 values=4/3/4294967289")

    def test_decodes_version_five_header(self):
        # Patch the version field and refresh the header CRC.
        data = bytearray(fixture_v3())
        struct.pack_into(">H", data, 4, 5)
        struct.pack_into(">I", data, 60, binascii.crc32(data[:60]) & 0xFFFFFFFF)
        snapshot = decoder.parse_snapshot(data)
        self.assertEqual(snapshot["version"], 5)
        self.assertEqual(snapshot["build_id"], "")
        self.assertEqual(len(snapshot["records"]), 4)

    def test_decodes_version_six_header_and_build_id(self):
        snapshot = decoder.parse_snapshot(fixture_v6())

        self.assertEqual(snapshot["version"], 6)
        self.assertEqual(snapshot["build_id"], "0123456789ab-dirty")
        self.assertEqual(snapshot["session_id"], 0x12345678)
        self.assertEqual(snapshot["records"][0]["event"], "log-flush")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            decoder.print_snapshot(snapshot)
        self.assertIn(" build=0123456789ab-dirty\n", output.getvalue())

    def test_build_id_may_fill_its_field(self):
        snapshot = decoder.parse_snapshot(fixture_v6(b"B" * 24))

        self.assertEqual(snapshot["build_id"], "B" * 24)

    def test_rejects_version_six_with_the_old_header_size(self):
        data = bytearray(fixture_v6())
        struct.pack_into(">H", data, 6, 64)
        with self.assertRaisesRegex(ValueError, "incompatible record layout"):
            decoder.parse_snapshot(bytes(data))

    def test_rejects_corrupt_version_six_header(self):
        data = bytearray(fixture_v6())
        data[60] ^= 1
        with self.assertRaisesRegex(ValueError, "header checksum"):
            decoder.parse_snapshot(bytes(data))

    def test_describes_link_session_events(self):
        state = decoder.describe({
            "event": "link-state", "flags": 1, "values": [2, 2, 5],
        })
        command = decoder.describe({
            "event": "link-command", "flags": 0,
            "values": [0x42504745, 0, 0],
        })
        error = decoder.describe({
            "event": "link-error", "flags": 7,
            "values": [2, 0x80410709, 13],
        })
        counters = decoder.describe({
            "event": "link-counters", "flags": 0,
            "values": [6200, 41000, 3],
        })
        pause = decoder.describe({
            "event": "link-pause-end", "flags": 1,
            "values": [125000, 2, 4],
        })

        self.assertEqual(
            state, "role=host status=connected mode=pokemon adhoc=connected")
        self.assertEqual(command, "role=join game=BPGE")
        self.assertEqual(
            error,
            "end=network status=connected "
            "adhoc-error=-2143222007/0x80410709 adhoc-phase=send")
        self.assertEqual(counters, "rtt-us/rtt-max-us/resends=6200/41000/3")
        self.assertEqual(
            decoder.describe({"event": "link-counters", "flags": 9,
                              "values": [3600, 2, 4100]}),
            "me-frames/frames-late/me-busy-us-per-frame=3600/2/4100")
        self.assertEqual(
            decoder.describe({"event": "link-counters", "flags": 10,
                              "values": [310, 96, 1]}),
            "log-us-per-frame/log-words-per-frame/full-resends=310/96/1")
        self.assertEqual(
            decoder.describe({"event": "link-counters", "flags": 11,
                              "values": [1, 0, 0]}),
            "exact-renderer=media-engine me-fault-cause=0x00000000 "
            "me-fault-address=0x00000000")
        self.assertEqual(
            pause, "mode=lockstep wait=125.000ms status=connected backlog=4")

    # The decoder's name tables must follow the C enum order.
    def test_link_names_match_the_c_enums(self):
        header = (Path(__file__).parents[1] / "source" / "src" /
                  "link_cable.h").read_text()
        for name in ("OFF", "WAITING", "CONNECTED", "INCOMPATIBLE",
                     "PEER_LEFT", "LOST"):
            self.assertIn(f"LINK_CABLE_{name}", header)
        self.assertEqual(len(decoder.LINK_STATUS), 6)
        self.assertEqual(decoder.LINK_MODES, ["none", "lockstep", "pokemon"])

    def test_rom_open_names_the_cpu_core(self):
        header = (Path(__file__).parents[1] / "source" / "src" /
                  "main.h").read_text()
        self.assertIn("#define CPU_CORE_CLASSIC 0", header)
        self.assertIn("#define CPU_CORE_NEW     1", header)
        self.assertEqual(
            decoder.describe({"event": "rom-open", "flags": 1,
                              "values": [0x414D4B50, 4194304, 1]}),
            "code=AMKP revision=1 size=4194304 core=new")
        self.assertEqual(
            decoder.describe({"event": "rom-open", "flags": 0,
                              "values": [0x42505249, 16777216, 0]}),
            "code=BPRI revision=0 size=16777216 core=classic")
        self.assertEqual(
            decoder.describe({"event": "link-counters", "flags": 12,
                              "values": [1, 8116, 2]}),
            "cpu-core=new blocks-compiled=8116 code-flushes=2")

    def test_decodes_diag_autorun_record(self):
        # Event 26 is appended to the enum, so the file stays version 6.
        record = struct.pack(">QIIHBBIII", 110, 7, 1, 26, 1, 0, 10, 2, 0)
        snapshot = decoder.parse_snapshot(fixture_v6(record=record))

        self.assertEqual(snapshot["version"], 6)
        self.assertEqual(snapshot["records"][0]["event"], "diag-autorun")
        self.assertEqual(
            decoder.describe(snapshot["records"][0]),
            "status=ignored reason=build-mismatch line=2 frames=0")
        self.assertEqual(
            decoder.describe({"event": "diag-autorun", "flags": 3,
                              "values": [0, 0, 3600]}),
            "status=done reason=none line=0 frames=3600")
        self.assertEqual(
            decoder.describe({"event": "diag-autorun", "flags": 9,
                              "values": [99, 0, 0]}),
            "status=9 reason=99 line=0 frames=0")

    def test_autorun_names_match_the_c_module(self):
        source = Path(__file__).parents[1] / "source" / "src"
        header = (source / "autorun_config.h").read_text()
        table = re.search(r"autorun_config_error_names\[\] =\s*\{(.*?)\};",
                          (source / "autorun_config.c").read_text(), re.S)
        errors = re.findall(r"^\s+(AUTORUN_(?:OK|ERROR_[A-Z_]+)),?$", header,
                            re.M)
        statuses = re.findall(r"^\s+AUTORUN_STATUS_([A-Z]+),?$", header, re.M)

        self.assertEqual(re.findall(r'"([a-z-]+)"', table.group(1)),
                         decoder.AUTORUN_REASONS)
        self.assertEqual(len(errors), len(decoder.AUTORUN_REASONS))
        self.assertEqual([status.lower() for status in statuses],
                         decoder.AUTORUN_STATUS)

    def test_decodes_net_fault_counters(self):
        # The ad-hoc worker logs counters 13-15 to the network ring; new
        # counter kinds keep the file at version 6.
        record = struct.pack(">QIIHBBIII", 110, 0, 1, 16, 2, 13,
                             2, 0x89ABCDEF, 0)
        snapshot = decoder.parse_snapshot(fixture_v6(record=record))

        self.assertEqual(snapshot["version"], 6)
        self.assertEqual(snapshot["records"][0]["source"], "network")
        self.assertEqual(snapshot["records"][0]["event"], "link-counters")
        self.assertEqual(decoder.describe(snapshot["records"][0]),
                         "net-fault-profile=noisy seed=0x89ABCDEF")
        self.assertEqual(
            decoder.describe({"event": "link-counters", "flags": 13,
                              "values": [6, 1, 0]}),
            "net-fault-profile=6 seed=0x00000001")
        self.assertEqual(
            decoder.describe({"event": "link-counters", "flags": 14,
                              "values": [41, 230, 12]}),
            "fault-out-lost/delayed/duplicated=41/230/12")
        self.assertEqual(
            decoder.describe({"event": "link-counters", "flags": 15,
                              "values": [7, 198, 3]}),
            "fault-in-lost/delayed/duplicated=7/198/3")

    def test_net_fault_profiles_match_the_c_table(self):
        text = (Path(__file__).parents[1] / "source" / "src" /
                "net_fault.c").read_text()
        table = re.search(
            r"net_fault_profiles\[NET_FAULT_PROFILE_COUNT\] =\s*\{(.*?)\n\};",
            text, re.S)
        self.assertEqual(re.findall(r'\{ "([a-z]+)"', table.group(1)),
                         decoder.NET_FAULT_PROFILES)

    def test_exact_renderer_names_match_the_c_enum(self):
        header = (Path(__file__).parents[1] / "source" / "src" /
                  "video_me.h").read_text()
        states = re.findall(r"^\s+(VIDEO_ME_[A-Z_]+)", header, re.M)
        self.assertEqual(len(states), len(decoder.EXACT_RENDERER))
        self.assertEqual(states[:2], ["VIDEO_ME_NOT_STARTED",
                                      "VIDEO_ME_DRAWING"])


if __name__ == "__main__":
    unittest.main()
