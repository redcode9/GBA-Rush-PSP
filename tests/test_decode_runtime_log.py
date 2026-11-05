"""Tests for tools/decode_runtime_log.py on hand-built .gbrlog files.

The fixtures pack the header and records the way runtime_log.c does; the
payload CRC goes at byte 56 and the header CRC (over bytes 0-59) at 60.
"""

import binascii
import importlib.util
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

    def test_decodes_power_bits_record(self):
        # Patch the first record of the v3 fixture into a version 5
        # power-bits record and refresh both checksums.
        data = bytearray(fixture_v3())
        struct.pack_into(">H", data, 4, 5)
        struct.pack_into(">HBBIII", data, 64 + 16, 26, 1, 0,
                         0x80001080, 2, 2)
        struct.pack_into(">I", data, 56, binascii.crc32(data[64:]) & 0xFFFFFFFF)
        struct.pack_into(">I", data, 60, binascii.crc32(data[:60]) & 0xFFFFFFFF)
        record = decoder.parse_snapshot(data)["records"][0]

        self.assertEqual(record["event"], "power-bits")
        self.assertEqual(
            decoder.describe(record),
            "bits=0x80001080 (switch) callback=2 "
            "outcome=entered-by-suspending")
        # A wake callback that carries the switch, as its own record.
        self.assertEqual(
            decoder.describe({"event": "power-bits", "flags": 0,
                              "values": [0x80040000, 4, 0]}),
            "bits=0x80040000 (switch|resume-complete) callback=4 "
            "outcome=none")
        self.assertEqual(
            decoder.describe({"event": "power-bits", "flags": 0,
                              "values": [0, 0, 3]}),
            "bits=0x00000000 (-) callback=0 outcome=switch-timed-out")

    # Events are numbered by their place in the C enum, so a name table out
    # of step (an event appended on two branches) shows here.
    def test_event_names_follow_the_c_enum(self):
        header = (Path(__file__).parents[1] / "source" / "src" /
                  "runtime_log.h").read_text()
        body = re.search(r"typedef enum\s*\{([^}]*)\}\s*RuntimeLogEventType;",
                         header).group(1)
        names = re.findall(r"RUNTIME_LOG_EVENT_(\w+)", body)
        self.assertIn("RUNTIME_LOG_EVENT_SESSION_START = 1,", body)
        self.assertEqual(len(names), len(decoder.EVENTS))
        for number, name in enumerate(names, start=1):
            self.assertEqual(decoder.EVENTS[number],
                             name.lower().replace("_", "-"))

    def test_exact_renderer_names_match_the_c_enum(self):
        header = (Path(__file__).parents[1] / "source" / "src" /
                  "video_me.h").read_text()
        states = re.findall(r"^\s+(VIDEO_ME_[A-Z_]+)", header, re.M)
        self.assertEqual(len(states), len(decoder.EXACT_RENDERER))
        self.assertEqual(states[:2], ["VIDEO_ME_NOT_STARTED",
                                      "VIDEO_ME_DRAWING"])


if __name__ == "__main__":
    unittest.main()
