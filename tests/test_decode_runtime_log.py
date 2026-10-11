import binascii
import importlib.util
import struct
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "tools" / "decode_runtime_log.py"
SPEC = importlib.util.spec_from_file_location("decode_runtime_log", MODULE_PATH)
decoder = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(decoder)


def fixture() -> bytes:
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
    def test_version_four_session_fields_preserve_full_detail(self):
        packed = (12 << 24) | (15 << 16) | (3 << 8) | 0x12
        detail = decoder.describe({"event": "link-counters", "flags": 26,
                                   "values": [15, 3, 0x212]})
        self.assertIn("state=recovering reason=transport", detail)
        self.assertIn("detail=530/0x0212", detail)
        text = decoder.describe({"event": "link-counters", "flags": 2,
                                 "values": [7, 8, packed]})
        self.assertIn("heartbeat-tx=7 heartbeat-rx=8", text)
        self.assertIn("message=12 state=recovering reason=transport", text)

    def test_health_is_an_exclusive_state_and_old_logs_remain_raw(self):
        record = {"event": "link-counters", "flags": 20, "values": [0, 0, 3]}
        self.assertIn("health=recovering", decoder.describe(record))
        self.assertNotIn("degraded=", decoder.describe(record))
        record["version"] = 3
        self.assertIn("health-raw=0x00000003", decoder.describe(record))

    def test_decodes_version_four_header(self):
        data = bytearray(fixture_v3())
        struct.pack_into(">H", data, 4, 4)
        struct.pack_into(">I", data, 60, binascii.crc32(data[:60]) & 0xFFFFFFFF)
        snapshot = decoder.parse_snapshot(data)
        self.assertEqual(snapshot["version"], 4)
        self.assertTrue(all(r["version"] == 4 for r in snapshot["records"]))

    def test_link_state_names_match_enum_order(self):
        self.assertEqual(decoder.LINK_STATES, [
            "idle", "hosting", "joining", "negotiating", "wait-ready",
            "wait-ready-ack", "connected", "wait-prepare", "wait-accept",
            "wait-commit", "wait-commit-ack", "wait-commit-release",
            "wait-release-ack", "wait-abort-ack", "wait-stream-reply",
            "recovering", "wait-resume-ack", "wait-resume-done",
            "disconnected", "error",
        ])

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
        self.assertEqual(snapshot["records"][1]["event"], "link-checkpoint")

    def test_describes_actual_sio_checkpoint(self):
        record = {
            "event": "link-checkpoint",
            "flags": 9,
            "values": [
                2 | (1 << 8) | (2 << 16) | (1 << 24) | (1 << 25),
                0x608B,
                0x0007,
            ],
        }

        description = decoder.describe(record)

        self.assertIn("actual-sio mode=2 player=1 count=2", description)
        self.assertIn("peer=True pending=True", description)
        self.assertIn("control=0x608B rcnt=0x0007", description)

    def test_describes_health_and_new_queue_diagnostics(self):
        health = decoder.describe({
            "event": "link-health",
            "flags": 1,
            "values": [7, 2034, 3],
        })
        critical = decoder.describe({
            "event": "link-counters",
            "flags": 19,
            "values": [(6 << 16) | 2, (4 << 16) | 1, 19],
        })

        self.assertEqual(
            health,
            "status=degraded generation=7 pause-age-ms=2034 epoch=3 "
            "causes=none",
        )
        self.assertIn("critical-handoff depth=2 high-water=6", critical)
        self.assertIn("pending-depth=1 pending-high-water=4", critical)
        self.assertIn("retry-suppressed=19", critical)

    def test_describes_backpressure_and_clock_diagnostics(self):
        episode = decoder.describe({
            "event": "link-counters",
            "flags": 21,
            "values": [6034, 579, 2],
        })
        maximum = decoder.describe({
            "event": "link-counters",
            "flags": 22,
            "values": [6034, 579, (3 << 16) | 3],
        })
        clock = decoder.describe({
            "event": "link-counters",
            "flags": 24,
            "values": [4, 1811, 3],
        })
        health = decoder.describe({
            "event": "link-health",
            "flags": 7,
            "values": [8, 6034, 2],
        })

        self.assertEqual(
            episode,
            "backpressure age=6034ms streak=579 episodes=2",
        )
        self.assertIn("last=state/clock", maximum)
        self.assertIn("causes=ack-stall+backpressure", clock)
        self.assertIn("causes=ack-stall+backpressure", health)

    def test_missing_new_counter_records_are_backward_compatible(self):
        snapshot = decoder.parse_snapshot(fixture_v2())

        self.assertFalse(any(
            record["event"] == "link-counters" and record["flags"] >= 21
            for record in snapshot["records"]
        ))

    def test_decodes_v3_attribution_timeout_and_metrics(self):
        snapshot = decoder.parse_snapshot(fixture_v3())
        descriptions = [decoder.describe(record) for record in snapshot["records"]]

        self.assertEqual(snapshot["version"], 3)
        self.assertIn(
            "attempt=4 domain=session internal=-7/0xFFFFFFF9",
            descriptions,
        )
        self.assertIn(
            "attempt=4 api-phase=pdp-send api-error=-2143222007/0x80410709",
            descriptions,
        )
        self.assertIn(
            "attempt=4 source=active-transfer reason=timeout",
            descriptions,
        )
        self.assertIn(
            "barrier-max-us/hot-wake-max-us/audio-underruns=1234/87/0",
            descriptions,
        )

    def test_describes_bidirectional_heartbeat_metrics(self):
        request = decoder.describe({
            "event": "link-stream-metrics",
            "flags": 7,
            "values": [41, 5, 4],
        })
        reply = decoder.describe({
            "event": "link-stream-metrics",
            "flags": 8,
            "values": [3, 2, 1],
        })
        state = decoder.describe({
            "event": "link-stream-metrics",
            "flags": 9,
            "values": [14, 1, 12],
        })

        self.assertEqual(request, "heartbeat-request-tx/rx/reply-tx=41/5/4")
        self.assertEqual(reply, "heartbeat-reply-rx/duplicate/unmatched=3/2/1")
        self.assertEqual(state, "heartbeat-outstanding/pending/last-request=14/1/12")

    def test_old_stream_metric_flags_remain_decodable(self):
        description = decoder.describe({
            "event": "link-stream-metrics",
            "flags": 6,
            "values": [25, 80, 7],
        })

        self.assertEqual(description, "hot-ms/cold-ms/hot-wakes=25/80/7")

    def test_command_description_exposes_attempt_boundary(self):
        description = decoder.describe({
            "event": "link-command",
            "flags": 1,
            "values": [2, 0, 9],
        })

        self.assertEqual(description, "role=host adhoc=2 attempt=9")

    def test_ahead_pacing_pause_is_named(self):
        description = decoder.describe({
            "event": "link-pause-end",
            "flags": 0x80 | 2 | 8,
            "values": [3, 6000, 4000],
        })

        self.assertIn("causes=ahead-pacing+gameplay", description)
        self.assertIn("mask=0x0A", description)


if __name__ == "__main__":
    unittest.main()
