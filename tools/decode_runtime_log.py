#!/usr/bin/env python3
"""Decode GBA Rush PSP runtime diagnostic snapshots."""

from __future__ import annotations

import argparse
import binascii
import json
import struct
from collections import Counter
from pathlib import Path
from typing import Any, Iterable


HEADER = struct.Struct(">4sHHHHHHIIIIIIQQII")
RECORD = struct.Struct(">QIIHBBIII")
MAGIC = b"GBRL"
VERSIONS = {1, 2, 3}

SOURCES = {1: "main", 2: "network", 3: "critical"}
EVENTS = {
    1: "session-start",
    2: "session-end",
    3: "rom-open",
    4: "menu-presence",
    5: "power-suspend",
    6: "power-resume",
    7: "link-command",
    8: "link-state",
    9: "link-pause-begin",
    10: "link-pause-end",
    11: "link-error",
    12: "link-recovery",
    13: "link-resync",
    14: "link-request-accepted",
    15: "link-request-rejected",
    16: "link-counters",
    17: "performance-slow",
    18: "log-flush",
    19: "link-transfer-sample",
    20: "link-checkpoint",
    21: "link-health",
    22: "link-failure",
    23: "link-stream-metrics",
    24: "link-timeout",
    25: "link-benchmark",
}
FLUSH_REASONS = {
    1: "start",
    2: "link-failure",
    3: "disconnect",
    4: "suspend",
    5: "rom-change",
    6: "menu",
    7: "exit",
    8: "link-checkpoint",
}
LINK_STATES = [
    "idle", "hosting", "joining", "negotiating", "wait-ready",
    "wait-ready-ack", "connected", "wait-prepare", "wait-accept",
    "wait-commit", "wait-commit-ack", "wait-commit-release",
    "wait-release-ack", "wait-abort-ack", "wait-stream-reply",
    "recovering", "wait-resume-ack", "wait-resume-done", "disconnected",
    "error",
]
LINK_REASONS = [
    "none", "user", "timeout", "transport", "protocol", "incompatible",
    "mode-mismatch", "queue-full", "peer-closed", "suspend", "rom-change",
]
LINK_TX_KINDS = ["none", "ack", "critical", "state", "probe"]
LINK_STATE_CLASSES = ["none", "sio", "heartbeat", "clock"]
ERROR_DOMAINS = [
    "none", "psp-api", "transport", "session", "lifecycle", "internal",
]
ADHOC_PHASES = [
    "none", "wlan", "thread", "load-common", "load-adhoc", "net-init",
    "adhoc-init", "control-init", "host-create", "control-wait",
    "mac-address", "scan-start", "scan-wait", "scan-results",
    "join-group", "peer-list", "pdp-create", "pdp-handshake", "pdp-send",
    "pdp-receive", "transport-recover", "group-recover", "stack-recover",
    "recovery-failed", "health-check", "shutdown",
]
TIMEOUT_SOURCES = [
    "none", "receive-silence", "active-transfer", "ahead-pacing", "pressure",
    "recovery-rendezvous", "shutdown", "negotiation", "abort-ack",
    "control", "peer-discovery", "connect-handshake",
]


def _crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def _name(names: list[str], value: int) -> str:
    return names[value] if value < len(names) else str(value)


def _signed(value: int) -> int:
    return value if value < 0x80000000 else value - 0x100000000


def _rom_code(identity: int) -> str:
    raw = identity.to_bytes(4, "big")
    if all(32 <= byte < 127 for byte in raw):
        return raw.decode("ascii", "replace")
    return f"0x{identity:08X}"


def _pause_causes(flags: int) -> str:
    labels = []
    for bit, label in (
        (1, "sio-dependency"),
        (2, "ahead-pacing"),
        (4, "barrier"),
        (8, "gameplay"),
        (16, "recovery"),
    ):
        if flags & bit:
            labels.append(label)
    return "+".join(labels) if labels else "none"


def parse_snapshot(data: bytes, path: str = "<memory>") -> dict[str, Any]:
    if len(data) < HEADER.size:
        raise ValueError(f"{path}: truncated header")
    fields = HEADER.unpack_from(data)
    (magic, version, header_size, record_size, critical_count, main_count,
     network_count, session_id, flush_reason, model, firmware, cpu_clock,
     rom_identity, started_us, flushed_us, payload_crc, header_crc) = fields
    if magic != MAGIC or version not in VERSIONS:
        raise ValueError(f"{path}: unsupported log format")
    if version == 1:
        critical_count = 0
    if header_size != HEADER.size or record_size != RECORD.size:
        raise ValueError(f"{path}: incompatible record layout")
    expected = header_size + (main_count + network_count + critical_count) * record_size
    if len(data) != expected:
        raise ValueError(f"{path}: expected {expected} bytes, found {len(data)}")
    if _crc32(data[:60]) != header_crc:
        raise ValueError(f"{path}: header checksum mismatch")
    if _crc32(data[header_size:]) != payload_crc:
        raise ValueError(f"{path}: payload checksum mismatch")

    records = []
    offset = header_size
    for _ in range(main_count + network_count + critical_count):
        values = RECORD.unpack_from(data, offset)
        timestamp, cycle, ordinal, event, source, flags = values[:6]
        value0, value1, value2 = values[6:]
        records.append({
            "timestamp_us": timestamp,
            "elapsed_us": timestamp - started_us if timestamp >= started_us else None,
            "emulated_cycle": cycle,
            "ordinal": ordinal,
            "event_id": event,
            "event": EVENTS.get(event, f"event-{event}"),
            "source_id": source,
            "source": SOURCES.get(source, f"source-{source}"),
            "flags": flags,
            "values": [value0, value1, value2],
        })
        offset += record_size
    records.sort(key=lambda record: (record["timestamp_us"], record["source_id"], record["ordinal"]))
    return {
        "path": path,
        "version": version,
        "session_id": session_id,
        "flush_reason_id": flush_reason,
        "flush_reason": FLUSH_REASONS.get(flush_reason, str(flush_reason)),
        "psp_model": model,
        "firmware": f"0x{firmware:08X}",
        "cpu_clock_mhz": cpu_clock,
        "rom_identity": _rom_code(rom_identity),
        "main_count": main_count,
        "network_count": network_count,
        "critical_count": critical_count,
        "started_us": started_us,
        "flushed_us": flushed_us,
        "records": records,
    }


def describe(record: dict[str, Any]) -> str:
    event = record["event"]
    flags = record["flags"]
    value0, value1, value2 = record["values"]
    if event == "link-state":
        return f"state={_name(LINK_STATES, value0)} reason={_name(LINK_REASONS, value1)} detail={value2} events=0x{flags:02X}"
    if event == "link-pause-end" and flags & 0x80:
        causes = flags & 0x7F
        return (
            f"waits={value0} total={value1 / 1000:.3f}ms "
            f"max={value2 / 1000:.3f}ms causes={_pause_causes(causes)} "
            f"mask=0x{causes:02X}"
        )
    if event == "performance-slow" and flags & 0x80:
        state = _name(LINK_STATES, value2 & 0xFFFF)
        causes = flags & 0x7F
        return (
            f"max={value0 / 1000:.3f}ms lead={_signed(value1)} "
            f"waits={value2 >> 16} state={state} "
            f"causes={_pause_causes(causes)} mask=0x{causes:02X}"
        )
    if event in ("link-pause-end", "performance-slow"):
        return (
            f"wait={value0 / 1000:.3f}ms lead={_signed(value1)} "
            f"state={_name(LINK_STATES, value2)} "
            f"causes={_pause_causes(flags)} mask=0x{flags:02X}"
        )
    if event == "link-error":
        return f"error={_signed(value0)} reason/state={value1} detail/phase={value2} flags=0x{flags:02X}"
    if event == "link-command":
        return (
            f"role={'host' if flags else 'join'} adhoc={value0} "
            f"attempt={value2}"
        )
    if event == "menu-presence":
        return f"gameplay={bool(value0)} link={_name(LINK_STATES, value1)} adhoc={value2}"
    if event == "rom-open":
        return f"code={_rom_code(value0)} revision={flags} size={value1}"
    if event == "link-counters":
        if flags in (16, 17, 18):
            mailbox = {16: "sio", 17: "heartbeat", 18: "clock"}[flags]
            return (
                f"{mailbox}-mailbox depth={value0 & 0xFFFF} "
                f"high-water={value0 >> 16} published={value1} "
                f"coalesced={value2}"
            )
        if flags == 19:
            return (
                f"critical-handoff depth={value0 & 0xFFFF} "
                f"high-water={value0 >> 16} "
                f"pending-depth={value1 & 0xFFFF} "
                f"pending-high-water={value1 >> 16} "
                f"retry-suppressed={value2}"
            )
        if flags == 20:
            return (
                f"last-transfer reason={_name(LINK_REASONS, value0)} "
                f"detail={value1} degraded={bool(value2 & 1)} "
                f"recovery-requested={bool(value2 & 2)} "
                f"recovering={bool(value2 & 4)}"
            )
        if flags == 21:
            return (
                f"backpressure age={value0}ms streak={value1} "
                f"episodes={value2}"
            )
        if flags == 22:
            return (
                f"backpressure-max age={value0}ms streak={value1} "
                f"last={_name(LINK_TX_KINDS, value2 & 0xFFFF)}/"
                f"{_name(LINK_STATE_CLASSES, value2 >> 16)}"
            )
        if flags == 23:
            return (
                f"clock-backoff current={value0}us next={value1}us "
                f"success-streak={value2}"
            )
        if flags == 24:
            causes = []
            if value2 & 1:
                causes.append("ack-stall")
            if value2 & 2:
                causes.append("backpressure")
            return (
                f"clock-pressure raises={value0} deferred={value1} "
                f"causes={'+'.join(causes) if causes else 'none'}"
            )
        if flags == 25:
            return (
                f"session-recovery requests={value0} "
                f"reason={_name(LINK_REASONS, value1)}"
            )
        labels = {
            0: "resync/group/stack",
            1: "tx/rx/retransmit",
            2: "heartbeat-tx/heartbeat-rx/session",
            3: "epoch/last-rx/outstanding",
            4: "queue-depth/coalesced/rejected",
            5: "recovery/resync/transition",
            6: "queue-high-water/backpressure/api-retries",
            7: "duplicates/out-of-window/invalid",
            8: "acks/probes/rto-us",
            9: "cpu/bus/tx-base",
            10: "base-attempts/message/age-ms",
            11: "next-tx/next-rx/buffered",
            12: "rx-data/rx-ack/ack-advances",
            13: "ack-age-ms/sent-bytes/received-bytes",
            14: "sio-started/committed/aborted",
            15: "sio-irqs/state/cycles",
        }
        return f"{labels.get(flags, 'counters')}={value0}/{value1}/{value2}"
    if event == "link-transfer-sample":
        mode = flags & 0x03
        role = "host" if flags & 0x04 else "join"
        path = "fast" if flags & 0x08 else "transactional"
        participants = (flags >> 4) & 0x0F
        return (
            f"sequence={value0} mode={mode} role={role} path={path} "
            f"participants=0x{participants:X} data={value1:08X}/{value2:08X}"
        )
    if event == "link-checkpoint":
        if flags == 0:
            return (
                f"state={_name(LINK_STATES, value0)} "
                f"reason={_name(LINK_REASONS, value1)} detail={value2}"
            )
        if flags == 1:
            return f"role={value0} capabilities=0x{value1:08X} invariants=0x{value2:08X}"
        if flags == 2:
            return f"last/active/applied-sequence={value0}/{value1}/{value2}"
        if flags == 3:
            return (
                f"transaction-sequence={value0} hash=0x{value1:08X} "
                f"stage={value2 & 0xFF} applied={bool(value2 & 0x100)} "
                f"deferred={bool(value2 & 0x200)}"
            )
        if flags in (4, 5):
            side = "local" if flags == 4 else "peer"
            return (
                f"{side}-sio mode={value0 & 0xFF} player={(value0 >> 8) & 0xFF} "
                f"count={(value0 >> 16) & 0xFF} flags=0x{value0 >> 24:02X} "
                f"valid={bool(value1 & 0x80000000)} "
                f"control=0x{value1 & 0xFFFF:04X} rcnt=0x{value2:04X}"
            )
        if flags == 6:
            return f"error={_signed(value0)} phase={value1} transition={value2}"
        if flags == 7:
            return (
                f"epoch={value0} last-rx={value1} "
                f"outstanding={value2 >> 16} buffered={value2 & 0xFFFF}"
            )
        if flags == 8:
            return f"state/barrier/rx-age-ms={value0}/{value1}/{value2}"
        if flags == 9:
            return (
                f"actual-sio mode={value0 & 0xFF} player={(value0 >> 8) & 0xFF} "
                f"count={(value0 >> 16) & 0xFF} peer={bool(value0 & 0x01000000)} "
                f"pending={bool(value0 & 0x02000000)} "
                f"waiting={bool(value0 & 0x04000000)} "
                f"active={bool(value0 & 0x08000000)} "
                f"control=0x{value1 & 0xFFFF:04X} rcnt=0x{value2 & 0xFFFF:04X}"
            )
        if flags == 15:
            return (
                f"last-transfer reason={_name(LINK_REASONS, value0)} "
                f"detail={value1} degraded={bool(value2 & 1)} "
                f"recovery-requested={bool(value2 & 2)} "
                f"recovering={bool(value2 & 4)} "
                f"pause-age-ms={value2 >> 8}"
            )
        if flags == 16:
            return (
                f"journal checkpoint={value0} hash=0x{value1:08X} "
                f"role={value2 & 0xFF} stage={(value2 >> 8) & 0xFF} "
                f"applied={bool(value2 & 0x10000)} "
                f"irq={bool(value2 & 0x20000)} "
                f"release={bool(value2 & 0x40000)} "
                f"valid={bool(value2 & 0x80000)}"
            )
        if flags == 17:
            return (
                f"journal active-sequence={value0} "
                f"transfer-hash=0x{value1:08X} epoch={value2}"
            )
        return f"checkpoint-{flags}={value0}/{value1}/{value2}"
    if event == "link-health":
        causes = []
        if flags & 2:
            causes.append("ack-stall")
        if flags & 4:
            causes.append("backpressure")
        return (
            f"status={'degraded' if flags & 1 else 'healthy'} "
            f"generation={value0} pause-age-ms={value1} epoch={value2} "
            f"causes={'+'.join(causes) if causes else 'none'}"
        )
    if event == "link-failure":
        if flags == 0:
            return (
                f"attempt={value0} domain={_name(ERROR_DOMAINS, value1)} "
                f"internal={_signed(value2)}/0x{value2:08X}"
            )
        if flags == 1:
            return (
                f"attempt={value0} api-phase={_name(ADHOC_PHASES, value1)} "
                f"api-error={_signed(value2)}/0x{value2:08X}"
            )
        if flags == 2:
            return (
                f"attempt={value0} internal-phase={_name(ADHOC_PHASES, value1)} "
                f"terminal={_signed(value2)}/0x{value2:08X}"
            )
    if event == "link-timeout":
        return (
            f"attempt={value0} source={_name(TIMEOUT_SOURCES, value1)} "
            f"reason={_name(LINK_REASONS, value2)}"
        )
    if event == "link-stream-metrics":
        labels = {
            0: "attempt/start-tx/reply-tx",
            1: "apply/duplicate/replay",
            2: "stale-generation/rtt-us/rto-us",
            3: "burst-last-us/burst-max-us/barrier-last-us",
            4: "barrier-max-us/hot-wake-max-us/audio-underruns",
            5: "hot-drain-packets/hot-yields/drain-count-high-water",
            6: "hot-ms/cold-ms/hot-wakes",
            7: "heartbeat-request-tx/rx/reply-tx",
            8: "heartbeat-reply-rx/duplicate/unmatched",
            9: "heartbeat-outstanding/pending/last-request",
        }
        return f"{labels.get(flags, f'metrics-{flags}')}={value0}/{value1}/{value2}"
    if event == "link-benchmark":
        labels = {
            0: "run/status/failure",
            1: "run/duration-ms/completed",
            2: "run/words-per-second/digest",
            3: "run/rtt-p50-us/rtt-p95-us",
            4: "run/rtt-p99-us/burst-p99-us",
            5: "run/burst-max-us/checksum",
            6: "run/duplicates/replays",
            7: "run/stale-generations/timeouts",
            8: "run/retransmissions/pure-acks",
            9: "run/queue-high-water/hot-wake-max-us",
            10: "run/barrier-max-us/audio-underruns",
            11: "run/full-recoveries/starvation-max-us",
            12: "run/role/attempt",
            13: "run/outstanding/queue-depth",
            0x80: "start/run/requested-duration-ms/offered-words-per-second",
            0x81: "start/run/seed/role",
        }
        return f"{labels.get(flags, f'benchmark-{flags}')}={value0}/{value1}/{value2}"
    if event in ("link-recovery", "link-resync", "link-request-accepted", "link-request-rejected"):
        return f"flags=0x{flags:02X} values={value0}/{value1}/{value2}"
    if event == "log-flush":
        return f"reason={FLUSH_REASONS.get(value0, value0)} main={value1} network={value2}"
    return f"flags=0x{flags:02X} values={value0}/{value1}/{value2}"


def iter_log_paths(arguments: Iterable[str]) -> list[Path]:
    paths: list[Path] = []
    for argument in arguments:
        path = Path(argument)
        if path.is_dir():
            paths.extend(sorted(path.glob("session-*.gbrlog")))
        else:
            paths.append(path)
    return paths


def print_snapshot(snapshot: dict[str, Any]) -> None:
    print(
        f"{snapshot['path']}: session={snapshot['session_id']:08X} "
        f"flush={snapshot['flush_reason']} PSP={snapshot['psp_model']} "
        f"clock={snapshot['cpu_clock_mhz']}MHz ROM={snapshot['rom_identity']}"
    )
    for record in snapshot["records"]:
        elapsed = record["elapsed_us"]
        time_text = "unknown" if elapsed is None else f"+{elapsed / 1_000_000:9.3f}s"
        print(
            f"  {time_text} {record['source']:7} "
            f"{record['event']:23} {describe(record)}"
        )


def print_summary(snapshot: dict[str, Any]) -> None:
    records = snapshot["records"]
    counts = Counter(record["event"] for record in records)
    retained = Counter(record["source"] for record in records)
    latest = {}

    for record in records:
        latest[record["source"]] = max(
            latest.get(record["source"], 0), record["ordinal"]
        )
    print(
        f"{snapshot['path']}: session={snapshot['session_id']:08X} "
        f"flush={snapshot['flush_reason']} PSP={snapshot['psp_model']} "
        f"clock={snapshot['cpu_clock_mhz']}MHz ROM={snapshot['rom_identity']}"
    )
    print(
        "  retained="
        + ", ".join(
            f"{source}:{retained[source]}/{latest.get(source, 0)}"
            for source in ("main", "network", "critical")
            if retained[source]
        )
    )
    print(
        "  events="
        + ", ".join(f"{event}:{count}" for event, count in sorted(counts.items()))
    )
    selected = [
        record for record in records
        if record["event"] in {
            "link-error", "link-transfer-sample", "link-checkpoint",
        }
        or (record["event"] == "link-counters" and record["flags"] >= 14)
    ]
    for record in selected:
        elapsed = record["elapsed_us"]
        time_text = "unknown" if elapsed is None else f"+{elapsed / 1_000_000:.3f}s"
        print(
            f"  {time_text} {record['source']:8} "
            f"{record['event']:23} {describe(record)}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", help=".gbrlog files or a logs directory")
    output = parser.add_mutually_exclusive_group()
    output.add_argument("--json", action="store_true", help="write machine-readable JSON")
    output.add_argument("--summary", action="store_true", help="write a concise diagnostic summary")
    args = parser.parse_args()
    snapshots = [
        parse_snapshot(path.read_bytes(), str(path))
        for path in iter_log_paths(args.paths)
    ]
    if args.json:
        print(json.dumps(snapshots, indent=2))
    else:
        for index, snapshot in enumerate(snapshots):
            if index:
                print()
            if args.summary:
                print_summary(snapshot)
            else:
                print_snapshot(snapshot)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
