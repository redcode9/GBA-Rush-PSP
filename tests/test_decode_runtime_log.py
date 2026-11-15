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


def fixture_v6(*records: bytes,
               build_id: bytes = b"0123456789ab-dirty") -> bytes:
    """Version 6 log: the 96-byte header with a build id, then the given
    main-ring records, or one log-flush record."""
    payload = b"".join(records) or struct.pack(
        ">QIIHBBIII", 110, 7, 1, 18, 1, 0, 2, 1, 0
    )
    header = bytearray(96)
    struct.pack_into(
        ">4sHHHHHHIIIIIIQQ", header, 0, b"GBRL", 6, 96, 32, 0,
        len(payload) // 32, 0, 0x12345678, 2, 1, 0x06060110, 333,
        0x42505249, 100, 120,
    )
    header[56:56 + len(build_id)] = build_id
    struct.pack_into(">I", header, 88, binascii.crc32(payload) & 0xFFFFFFFF)
    struct.pack_into(">I", header, 92, binascii.crc32(header[:92]) & 0xFFFFFFFF)
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

    def test_decodes_rom_load_stages(self):
        # Every half above 255 and the read above 65535, so a narrower mask fails.
        values = (70000, 1200 << 16 | 300, 65535 << 16 | 4000)
        expected = ("open=1200ms read=70000ms config=300ms "
                    "backup-id=65535ms sav=4000ms")
        self.assertEqual(
            decoder.describe({"event": "rom-load-stages", "flags": 0,
                              "values": list(values)}), expected)
        snapshot = decoder.parse_snapshot(fixture_v6(
            struct.pack(">QIIHBBIII", 110, 7, 1, 32, 1, 0, *values)))

        self.assertEqual(snapshot["records"][0]["event"], "rom-load-stages")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            decoder.print_snapshot(snapshot)
        self.assertRegex(output.getvalue(), rf" rom-load-stages +{expected}\n")

    def test_build_id_may_fill_its_field(self):
        snapshot = decoder.parse_snapshot(fixture_v6(build_id=b"B" * 24))

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
        main = (Path(__file__).parents[1] / "source" / "src" /
                "main.h").read_text()
        ends = main[main.rindex("typedef enum", 0, main.index("} MainLinkEnd;")):
                    main.index("} MainLinkEnd;")]
        self.assertEqual(
            decoder.LINK_ENDS,
            [name.lower().replace("_", "-") for name in
             re.findall(r"^\s+MAIN_LINK_END_([A-Z_]+)", ends, re.M)])

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
        # Event 33 is appended to the enum, so the file stays version 6.
        record = struct.pack(">QIIHBBIII", 110, 7, 1, 33, 1, 0, 10, 2, 0)
        snapshot = decoder.parse_snapshot(fixture_v6(record))

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
        snapshot = decoder.parse_snapshot(fixture_v6(record))

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
    def test_describes_cpu_clock_records(self):
        main_c = (Path(__file__).parents[1] / "source" / "src" /
                  "main.c").read_text(encoding="utf-8")
        # Requested, then CPU, then bus, as the decoder reads them.
        self.assertRegex(
            main_c,
            r"cpu_ticks, 18,\s+mhz, "
            r"\(u32\)scePowerGetCpuClockFrequencyInt\(\),\s+"
            r"\(u32\)scePowerGetBusClockFrequencyInt\(\)\);")
        cases = [
            ([333, 333, 166], "requested=333MHz cpu=333MHz bus=166MHz"),
            ([222, 222, 111], "requested=222MHz cpu=222MHz bus=111MHz"),
            ([333, 332, 167], "requested=333MHz cpu=332MHz bus=167MHz"),
            ([333, 331, 166],
             "requested=333MHz cpu=331MHz bus=166MHz MISMATCH"),
            ([333, 333, 164],
             "requested=333MHz cpu=333MHz bus=164MHz MISMATCH"),
            ([333, 222, 111],
             "requested=333MHz cpu=222MHz bus=111MHz MISMATCH"),
            ([333, 300, 166],
             "requested=333MHz cpu=300MHz bus=166MHz MISMATCH"),
            ([333, 333, 111],
             "requested=333MHz cpu=333MHz bus=111MHz MISMATCH"),
            ([333, 0x8002013A, 166],
             "requested=333MHz cpu=-2147352262MHz bus=166MHz MISMATCH"),
        ]
        for values, expected in cases:
            with self.subTest(values=values):
                self.assertEqual(
                    decoder.describe({"event": "link-counters", "flags": 18,
                                      "values": values}),
                    "cpu-clock " + expected)

    def test_describes_me_clock_records(self):
        main_c = (Path(__file__).parents[1] / "source" / "src" /
                  "main.c").read_text(encoding="utf-8")
        # Requested MHz, then 2 * counter ticks / us rounded, then ms.
        self.assertRegex(
            main_c,
            r"cpu_ticks, 19,\s+main_clock_requested_mhz, "
            r"\(u32\)\(\(2 \* ticks \+ us / 2\) / us\),\s+"
            r"\(u32\)\(us / 1000\)\);")
        cases = [
            ([333, 333, 61234], "requested=333MHz me=333MHz over=61.2s"),
            ([222, 222, 2000], "requested=222MHz me=222MHz over=2.0s"),
            ([333, 332, 1000], "requested=333MHz me=332MHz over=1.0s"),
            ([333, 334, 1000], "requested=333MHz me=334MHz over=1.0s"),
            ([333, 331, 1000],
             "requested=333MHz me=331MHz over=1.0s MISMATCH"),
            ([333, 335, 1000],
             "requested=333MHz me=335MHz over=1.0s MISMATCH"),
            ([333, 222, 5000],
             "requested=333MHz me=222MHz over=5.0s MISMATCH"),
            # Under a millisecond is too short to trust.
            ([333, 333, 0], "requested=333MHz me=unmeasured"),
        ]
        for values, expected in cases:
            with self.subTest(values=values):
                self.assertEqual(
                    decoder.describe({"event": "link-counters", "flags": 19,
                                      "values": values}),
                    "me-clock " + expected)

    def test_describes_memory_stats_records(self):
        # flags = point << 1 | part; values in KiB.
        cases = [
            (2, [3072, 1024, 8192],
             "point=boot image=3072KiB heap-free=1024KiB heap-largest=8192KiB"),
            (3, [20480, 12288, 32768],
             "point=boot kernel-total=20480KiB kernel-largest=12288KiB "
             "rom-buffer=32768KiB rom-pages=1024"),
            (11, [0, 0, 4096],
             "point=capture-stop kernel-total=0KiB kernel-largest=0KiB "
             "rom-buffer=4096KiB rom-pages=128"),
            (14, [1, 2, 3],
             "point=7 image=1KiB heap-free=2KiB heap-largest=3KiB"),
        ]
        for flags, values, expected in cases:
            with self.subTest(flags=flags):
                self.assertEqual(
                    decoder.describe({"event": "memory-stats", "flags": flags,
                                      "values": values}),
                    expected)

    def test_decodes_memory_stats_in_a_version_six_log(self):
        record = struct.pack(
            ">QIIHBBIII", 110, 7, 1, 26, 1, 5, 21504, 12288, 16384)
        snapshot = decoder.parse_snapshot(fixture_v6(record))

        self.assertEqual(snapshot["records"][0]["event"], "memory-stats")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            decoder.print_snapshot(snapshot)
        self.assertIn(
            "memory-stats            point=rom-load kernel-total=21504KiB "
            "kernel-largest=12288KiB rom-buffer=16384KiB rom-pages=512\n",
            output.getvalue())

    # A bad merge of event types appended by two branches shows up here.
    def test_event_names_match_the_c_enum(self):
        header = (Path(__file__).parents[1] / "source" / "src" /
                  "runtime_log.h").read_text()
        body = header.split("typedef enum", 2)[2].split("} RuntimeLogEventType;")[0]
        names = re.findall(r"^\s*RUNTIME_LOG_EVENT_(\w+)", body, re.M)
        self.assertIn("RUNTIME_LOG_EVENT_SESSION_START = 1", body)
        self.assertEqual(
            decoder.EVENTS,
            {index: name.lower().replace("_", "-")
             for index, name in enumerate(names, 1)})

    def test_memory_points_match_the_c_enum(self):
        header = (Path(__file__).parents[1] / "source" / "src" /
                  "mem_stats.h").read_text()
        body = header.split("typedef enum", 1)[1].split("} MemStatsPoint;")[0]
        names = re.findall(r"^\s*MEM_STATS_(\w+)", body, re.M)
        self.assertIn("MEM_STATS_BOOT = 1", body)
        self.assertEqual(
            decoder.MEMORY_POINTS,
            ["none"] + [name.lower().replace("_", "-") for name in names])
    def test_decodes_save_load(self):
        # A 128 KiB file for a 64 KiB Flash game, then one that will not
        # read (event 27, flags = the load's result).
        snapshot = decoder.parse_snapshot(fixture_v6(
            struct.pack(">QIIHBBIII", 110, 0, 1, 27, 1, 0,
                        131072, 65536, 1 | 3 << 8 | 1 << 16),
            struct.pack(">QIIHBBIII", 111, 0, 2, 27, 1, 2, 0, 0, 3),
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

    def test_decodes_save_write(self):
        # A Flash 128 KiB flush on a full Memory Stick, its retry that
        # writes, and a result this decoder lacks (event 29).
        snapshot = decoder.parse_snapshot(fixture_v6(
            struct.pack(">QIIHBBIII", 110, 0, 1, 29, 1, 3,
                        41250, 131072, 1),
            struct.pack(">QIIHBBIII", 111, 0, 2, 29, 1, 0,
                        187003, 131072, 2),
            struct.pack(">QIIHBBIII", 112, 0, 3, 29, 1, 9, 5, 512, 1),
        ))
        failed, written, unknown = snapshot["records"]

        self.assertEqual(failed["event"], "save-write")
        self.assertEqual(
            decoder.describe(failed),
            "result=write-failed duration-us=41250 bytes=131072 attempt=1")
        self.assertEqual(
            decoder.describe(written),
            "result=ok duration-us=187003 bytes=131072 attempt=2")
        self.assertEqual(decoder.describe(unknown),
                         "result=9 duration-us=5 bytes=512 attempt=1")

    def test_decodes_save_link_copy(self):
        # The pre-link copy of a Flash 128 KiB save, one that failed and a
        # result this decoder lacks (event 30).
        snapshot = decoder.parse_snapshot(fixture_v6(
            struct.pack(">QIIHBBIII", 120, 0, 1, 30, 1, 0,
                        96500, 131072, 0),
            struct.pack(">QIIHBBIII", 121, 0, 2, 30, 1, 2,
                        1200, 32768, 0),
            struct.pack(">QIIHBBIII", 122, 0, 3, 30, 1, 9, 5, 512, 0),
        ))
        copied, failed, unknown = snapshot["records"]

        self.assertEqual(copied["event"], "save-link-copy")
        self.assertEqual(decoder.describe(copied),
                         "result=ok duration-us=96500 bytes=131072")
        self.assertEqual(decoder.describe(failed),
                         "result=failed duration-us=1200 bytes=32768")
        self.assertEqual(decoder.describe(unknown),
                         "result=9 duration-us=5 bytes=512")

    def test_save_load_names_match_the_c_enums(self):
        source = Path(__file__).parents[1] / "source" / "src"
        store = (source / "save_store.h").read_text()
        formats = (source / "save_format.h").read_text()

        def names(prefix, text):
            return [name.lower().replace("_", "-") for name in
                    re.findall(rf"^\s+{prefix}([A-Z0-9_]+)", text, re.M)]

        results = store[store.index("typedef enum"):
                        store.index("} SaveStoreResult;")]
        self.assertEqual(names("SAVE_STORE_", results), decoder.SAVE_RESULTS)
        self.assertEqual(names("SAVE_TYPE_", formats), decoder.SAVE_TYPES)
        self.assertEqual(names("SAVE_TAG_", formats), decoder.SAVE_TAGS)

    def test_decodes_savestate_repair(self):
        # A state with a bad Flash bank and EEPROM counter, then one whose
        # type went back to the one in use, plus a bit this decoder lacks
        # (event 28).
        snapshot = decoder.parse_snapshot(fixture_v6(
            struct.pack(">QIIHBBIII", 110, 0, 1, 28, 1, 0,
                        0x108, 1, 0),
            struct.pack(">QIIHBBIII", 111, 0, 2, 28, 1, 0,
                        0x201, 2, 0),
        ))
        flash, eeprom = snapshot["records"]

        self.assertEqual(flash["event"], "savestate-repair")
        self.assertEqual(decoder.describe(flash),
                         "fixed=flash-bank,eeprom-counter type=flash")
        self.assertEqual(decoder.describe(eeprom),
                         "fixed=type,0x200 type=eeprom")

    def test_savestate_repair_names_match_the_c_bits(self):
        formats = (Path(__file__).parents[1] / "source" / "src" /
                   "save_format.h").read_text()
        bits = re.findall(r"^#define SAVE_FIX_([A-Z_]+)\s+0x([0-9A-Fa-f]+)u",
                          formats, re.M)

        self.assertEqual(
            [(name.lower().replace("_", "-"), int(value, 16))
             for name, value in bits],
            [(name, 1 << bit) for bit, name in enumerate(decoder.SAVE_FIXES)])

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

    def test_decodes_power_bits_record(self):
        # Patch the first record of the v3 fixture into a version 5
        # power-bits record and refresh both checksums.
        data = bytearray(fixture_v3())
        struct.pack_into(">H", data, 4, 5)
        struct.pack_into(">HBBIII", data, 64 + 16, 31, 1, 0,
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
        # The same record in a version 6 log, the format builds write now.
        data = bytearray(fixture_v6())
        struct.pack_into(">HBBIII", data, 96 + 16, 31, 1, 0,
                         0x80001080, 2, 2)
        struct.pack_into(">I", data, 88, binascii.crc32(data[96:]) & 0xFFFFFFFF)
        struct.pack_into(">I", data, 92, binascii.crc32(data[:92]) & 0xFFFFFFFF)
        record = decoder.parse_snapshot(bytes(data))["records"][0]
        self.assertEqual(
            decoder.describe(record),
            "bits=0x80001080 (switch) callback=2 "
            "outcome=entered-by-suspending")

    def test_exact_renderer_names_match_the_c_enum(self):
        header = (Path(__file__).parents[1] / "source" / "src" /
                  "video_me.h").read_text()
        states = re.findall(r"^\s+(VIDEO_ME_[A-Z_]+)", header, re.M)
        self.assertEqual(len(states), len(decoder.EXACT_RENDERER))
        self.assertEqual(states[:2], ["VIDEO_ME_NOT_STARTED",
                                      "VIDEO_ME_DRAWING"])


if __name__ == "__main__":
    unittest.main()
