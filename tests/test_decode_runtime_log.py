"""Tests for tools/decode_runtime_log.py on hand-built .gbrlog files.

The fixtures pack the header and records the way runtime_log.c does; the
payload CRC and the header CRC (over the header bytes before it) are the
last 8 header bytes: 56 and 60 up to version 5, 88 and 92 from version 6.
"""

import binascii
import contextlib
import importlib.util
import io
import json
import re
import struct
import subprocess
import sys
import tempfile
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


def fixture_v6(build_id: bytes = b"0123456789ab-dirty") -> bytes:
    """Version 6 log: the 96-byte header with a build id, one record."""
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


def pack_snapshot(records: list[bytes], version: int) -> bytes:
    """A log of main-ring records with the header of version 5 or 6."""
    header_size = 96 if version >= 6 else 64
    payload = b"".join(records)
    header = bytearray(header_size)
    struct.pack_into(
        ">4sHHHHHHIIIIIIQQ", header, 0, b"GBRL", version, header_size, 32,
        0, len(records), 0, 0x12345678, 6, 1, 0x06060110, 333, 0x42505249,
        100, 120,
    )
    struct.pack_into(">I", header, header_size - 8,
                     binascii.crc32(payload) & 0xFFFFFFFF)
    struct.pack_into(">I", header, header_size - 4,
                     binascii.crc32(header[:header_size - 4]) & 0xFFFFFFFF)
    return bytes(header) + payload


def probe_records(entries) -> list[bytes]:
    """Name-probe records as runtime_log_name_probe_entry() emits them, then
    the end record. entries holds (d_name, FAT record bytes, open results)."""
    records = []

    def add(flags, *values):
        ordinal = len(records) + 1
        records.append(struct.pack(">QIIHBBIII", 200 + ordinal, 0, ordinal,
                                   26, 1, flags, *values))

    def chunks(part, data):
        for offset in range(0, len(data), 12):
            add(part << 6 | offset // 12,
                *struct.unpack(">III", data[offset:offset + 12].ljust(12, b"\0")))

    for index, (name, fat, opened) in enumerate(entries):
        add(0, index, len(name), len(fat))
        chunks(1, name)
        chunks(2, fat)
        add(0xC0, *(value & 0xFFFFFFFF for value in opened))
    add(0x01, 0x00ABCDEF, len(entries), len(entries))
    return records


NOT_TRIED = -1
PSP_ERROR = 0x80010002 - 0x100000000
# Japanese UTF-8 in layout A (alias at 0, long name at 13); Chinese GBK and
# accented UTF-8 in layout B (size word, alias at 4, long name at 20).
PROBE_ENTRIES = [
    ("ポケモン.gba".encode("utf-8"),
     b"POKEMO~1.GBA\0" + "ポケモン.gba".encode("utf-8"),
     (3, 4, PSP_ERROR)),
    ("宝可梦.gba".encode("gbk"),
     (1044).to_bytes(4, "little") + b"BAOKEM~1.GBA".ljust(16, b"\0")
     + "宝可梦.gba".encode("utf-8"),
     (PSP_ERROR, NOT_TRIED, 5)),
    ("Pokémon Smeraldo.gba".encode("utf-8"),
     (1044).to_bytes(4, "little") + b"POKMON~1.GBA".ljust(16, b"\0")
     + "Pokémon Smeraldo.gba".encode("utf-8"),
     (6, NOT_TRIED, 7)),
]


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

    def test_exact_renderer_names_match_the_c_enum(self):
        header = (Path(__file__).parents[1] / "source" / "src" /
                  "video_me.h").read_text()
        states = re.findall(r"^\s+(VIDEO_ME_[A-Z_]+)", header, re.M)
        self.assertEqual(len(states), len(decoder.EXACT_RENDERER))
        self.assertEqual(states[:2], ["VIDEO_ME_NOT_STARTED",
                                      "VIDEO_ME_DRAWING"])

    # Branches append events to the same enum; a renumbering must show here.
    def test_event_names_follow_the_c_enum(self):
        header = (Path(__file__).parents[1] / "source" / "src" /
                  "runtime_log.h").read_text()
        block = re.search(r"typedef enum\s*\{([^}]*)\}\s*RuntimeLogEventType;",
                          header).group(1)
        names = re.findall(r"RUNTIME_LOG_EVENT_(\w+)", block)
        self.assertIn("RUNTIME_LOG_EVENT_SESSION_START = 1,", block)
        self.assertEqual(list(decoder.EVENTS), list(range(1, len(names) + 1)))
        self.assertEqual(list(decoder.EVENTS.values()),
                         [name.lower().replace("_", "-") for name in names])

    def test_rebuilds_name_probe_entries(self):
        snapshot = decoder.parse_snapshot(
            pack_snapshot(probe_records(PROBE_ENTRIES), 6))
        entries = decoder.name_probe_entries(snapshot)

        self.assertEqual(len(entries), 3)
        for index, (entry, (name, fat, _)) in enumerate(
                zip(entries, PROBE_ENTRIES)):
            self.assertEqual(entry["index"], index)
            self.assertTrue(entry["complete"])
            self.assertEqual(entry["name"]["hex"], name.hex())
            self.assertEqual(entry["fat_hex"], fat.hex())
        japanese, chinese, accented = entries
        self.assertEqual(japanese["name"]["utf-8"], "ポケモン.gba")
        self.assertEqual(japanese["layout_a"]["alias"]["utf-8"], "POKEMO~1.GBA")
        self.assertEqual(japanese["layout_a"]["long_name"]["utf-8"],
                         "ポケモン.gba")
        self.assertEqual(japanese["open"],
                         {"name": 3, "alias-a": 4, "alias-b": -2147418110})
        self.assertEqual(chinese["name"]["gbk"], "宝可梦.gba")
        self.assertEqual(chinese["layout_b"]["size"], 1044)
        self.assertEqual(chinese["layout_b"]["alias"]["utf-8"], "BAOKEM~1.GBA")
        self.assertEqual(chinese["layout_b"]["long_name"]["utf-8"],
                         "宝可梦.gba")
        self.assertEqual(chinese["open"], {"name": -2147418110,
                                           "alias-a": "not-tried",
                                           "alias-b": 5})
        self.assertEqual(accented["name"]["utf-8"], "Pokémon Smeraldo.gba")
        self.assertEqual(accented["layout_b"]["long_name"]["utf-8"],
                         "Pokémon Smeraldo.gba")

    def test_name_probe_reads_the_same_from_version_five(self):
        records = probe_records(PROBE_ENTRIES)
        version6 = decoder.parse_snapshot(pack_snapshot(records, 6))
        version5 = decoder.parse_snapshot(pack_snapshot(records, 5))

        self.assertEqual(version5["version"], 5)
        self.assertEqual(decoder.name_probe_entries(version5),
                         decoder.name_probe_entries(version6))
        self.assertEqual([decoder.describe(record) for record in version5["records"]],
                         [decoder.describe(record) for record in version6["records"]])

    def test_name_probe_overwritten_records_mark_entries_incomplete(self):
        records = probe_records(PROBE_ENTRIES)
        # The ring overwrote the first entry's header and its first chunk.
        snapshot = decoder.parse_snapshot(pack_snapshot(records[2:], 6))
        entries = decoder.name_probe_entries(snapshot)

        self.assertEqual(len(entries), 3)
        self.assertIsNone(entries[0]["index"])
        self.assertFalse(entries[0]["complete"])
        self.assertTrue(entries[1]["complete"])
        # A chunk missing in the middle of an entry.
        missing = [record for record in records
                   if record[19] != 0x41 or struct.unpack_from(">I", record, 12)[0] > 8]
        entries = decoder.name_probe_entries(
            decoder.parse_snapshot(pack_snapshot(missing, 6)))
        self.assertFalse(entries[0]["complete"])
        self.assertEqual(len(entries[0]["name"]["hex"]), 2 * len(PROBE_ENTRIES[0][0]))
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            decoder.print_snapshot(decoder.parse_snapshot(pack_snapshot(missing, 6)))
        self.assertIn("name-probe entry 0 (incomplete)\n", output.getvalue())
        self.assertIn("name-probe entry 1\n", output.getvalue())

    def test_describes_name_probe_records(self):
        def describe(flags, *values):
            return decoder.describe({"event": "name-probe", "flags": flags,
                                     "values": list(values)})

        self.assertEqual(describe(0x00, 2, 16, 29),
                         "entry=2 name-bytes=16 fat-bytes=29")
        self.assertEqual(describe(0x41, 0x41424344, 0x45464748, 0x494A0000),
                         "name chunk=1 bytes=4142434445464748494a0000")
        self.assertEqual(describe(0x82, 0, 0, 1),
                         "fat chunk=2 bytes=000000000000000000000001")
        self.assertEqual(describe(0xC0, 3, 0x80010002, 0xFFFFFFFF),
                         "open name=fd 3 alias-a=error 0x80010002 "
                         "alias-b=not-tried")
        self.assertEqual(describe(0x01, 0x80010002, 4, 3),
                         "end dopen=-2147418110/0x80010002 seen=4 logged=3")

    def test_command_line_prints_and_exports_the_probe(self):
        data = pack_snapshot(probe_records(PROBE_ENTRIES), 6)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "session-1.gbrlog"
            path.write_bytes(data)
            text = subprocess.run(
                [sys.executable, str(MODULE_PATH), str(path)],
                capture_output=True, check=True).stdout.decode("utf-8", "replace")
            exported = json.loads(subprocess.run(
                [sys.executable, str(MODULE_PATH), "--json", str(path)],
                capture_output=True, check=True).stdout)
        self.assertIn("name-probe entry 2\n", text.replace("\r\n", "\n"))
        self.assertEqual(exported[0]["name_probe"][1]["layout_b"]["size"], 1044)


if __name__ == "__main__":
    unittest.main()
