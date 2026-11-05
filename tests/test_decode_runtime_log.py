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


def fixture_v5(*records: bytes) -> bytes:
    """Version 5 log of main-ring records."""
    payload = b"".join(records)
    header = bytearray(64)
    struct.pack_into(
        ">4sHHHHHHIIIIIIQQ", header, 0, b"GBRL", 5, 64, 32, 0,
        len(records), 0, 0x12345678, 1, 1, 0x06060110, 333, 0x42505249,
        100, 120,
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

    def test_decodes_save_load(self):
        # A 128 KiB file for a 64 KiB Flash game, then one that will not
        # read (event 26, flags = the load's result).
        snapshot = decoder.parse_snapshot(fixture_v5(
            struct.pack(">QIIHBBIII", 110, 0, 1, 26, 1, 0,
                        131072, 65536, 1 | 3 << 8 | 1 << 16),
            struct.pack(">QIIHBBIII", 111, 0, 2, 26, 1, 2, 0, 0, 3),
        ))
        loaded, failed = snapshot["records"]

        self.assertEqual(loaded["event"], "save-load")
        self.assertEqual(
            decoder.describe(loaded),
            "result=ok file-size=131072 write-size=65536 type=flash "
            "tag=flash64 mismatch=True")
        self.assertEqual(
            decoder.describe(failed),
            "result=failed file-size=0 write-size=0 type=none tag=none "
            "mismatch=False")

    def test_save_load_names_match_the_c_enums(self):
        source = Path(__file__).parents[1] / "source" / "src"
        store = (source / "save_store.h").read_text()
        formats = (source / "save_format.h").read_text()

        def names(prefix, text):
            return [name.lower().replace("_", "-") for name in
                    re.findall(rf"^\s+{prefix}([A-Z0-9_]+)", text, re.M)]

        self.assertEqual(names("SAVE_STORE_", store)[:3],
                         decoder.SAVE_LOAD_RESULTS)
        self.assertEqual(names("SAVE_TYPE_", formats), decoder.SAVE_TYPES)
        self.assertEqual(names("SAVE_TAG_", formats), decoder.SAVE_TAGS)

    # Events added at the end of the C enum keep the format version, so the
    # decoder's table must follow the enum exactly.
    def test_event_names_match_the_c_enum(self):
        header = (Path(__file__).parents[1] / "source" / "src" /
                  "runtime_log.h").read_text()
        events = re.findall(r"^\s+RUNTIME_LOG_EVENT_([A-Z_]+)", header, re.M)

        self.assertEqual(
            [event.lower().replace("_", "-") for event in events],
            [decoder.EVENTS[i] for i in range(1, len(decoder.EVENTS) + 1)])

    def test_link_counter_groups_decode_by_name(self):
        def counters(group):
            return decoder.describe({"event": "link-counters", "flags": group,
                                     "values": [1, 2, 3]})

        self.assertEqual(
            counters(7), "lockstep-priority/priority-seen/change-result=1/2/3")
        self.assertEqual(counters(8), "frames/frames-drawn/vblank-wait-us=1/2/3")
        # Groups 11 and 12 have lines of their own (Exact renderer, CPU
        # core); the next one is still unknown.
        unknown = max([*decoder.LINK_COUNTERS, 12]) + 1
        self.assertEqual(counters(unknown), f"counters-{unknown}=1/2/3")

    def test_exact_renderer_names_match_the_c_enum(self):
        header = (Path(__file__).parents[1] / "source" / "src" /
                  "video_me.h").read_text()
        states = re.findall(r"^\s+(VIDEO_ME_[A-Z_]+)", header, re.M)
        self.assertEqual(len(states), len(decoder.EXACT_RENDERER))
        self.assertEqual(states[:2], ["VIDEO_ME_NOT_STARTED",
                                      "VIDEO_ME_DRAWING"])


if __name__ == "__main__":
    unittest.main()
