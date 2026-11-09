#!/usr/bin/env python3
"""Decode GBA Rush PSP runtime diagnostic snapshots."""

from __future__ import annotations

import argparse
import binascii
import json
import struct
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Iterable

# The module docstring doubles as the --help description.

# Big-endian file written by source/src/runtime_log.c. Header (96 bytes
# from version 6, 64 before): magic, version, header size, record size,
# critical/main/network record counts, session id, flush reason, PSP model,
# firmware, CPU clock in MHz, ROM game code, start and flush times in us;
# from version 6 the build id (24 bytes of NUL-padded ASCII at 56) and 8
# reserved bytes. The header always ends with the payload CRC-32 and the
# CRC-32 of the header bytes before it. Record (32 bytes): timestamp in us,
# emulated cycle, ordinal, event, source, flags and three event-specific
# values.
HEADER = struct.Struct(">4sHHHHHHIIIIIIQQ")
RECORD = struct.Struct(">QIIHBBIII")
MAGIC = b"GBRL"
# The header size of each version this decoder reads.
HEADER_SIZES = {1: 64, 2: 64, 3: 64, 4: 64, 5: 64, 6: 96}
BUILD_ID = slice(56, 80)

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
    26: "name-probe",
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
# Link events from version 5 on, written by main.c's link glue.
LINK_STATUS = ["off", "waiting", "connected", "incompatible", "peer-left", "lost"]
LINK_MODES = ["none", "lockstep", "pokemon"]
LINK_ENDS = [
    "none", "user", "suspend", "rom-change", "peer-left", "lost",
    "incompatible", "network",
]
ADHOC_STATES = [
    "stopped", "starting", "hosting", "searching", "connecting", "connected",
    "failed",
]
ADHOC_PHASES = [
    "none", "wlan", "thread", "load-modules", "net-init", "adhoc-init",
    "control-init", "create-group", "scan", "join-group", "group-wait",
    "peer-list", "socket", "send", "receive", "group-lost",
]
LINK_COUNTERS = {
    0: "rtt-us/rtt-max-us/resends",
    1: "datagrams-sent/received/rejected",
    2: "pokemon-frames-sent/received/lockstep-transfers",
    3: "waits/wait-max-us/wait-total-us",
    4: "adhoc-sends/send-errors/receive-drops",
    5: "early-replies/mispredicted/trusted-streak",
    6: "rtt-min-us/send-yields/send-yield-us",
    7: "lockstep-priority/priority-seen/change-result",
    8: "frames/frames-drawn/vblank-wait-us",
    9: "me-frames/frames-late/me-busy-us-per-frame",
    10: "log-us-per-frame/log-words-per-frame/full-resends",
}
# The CPU core a start or reset chose (CPU_CORE_* in source/src/main.h);
# logs from before the option say classic.
CPU_CORES = ["classic", "new"]
# Where the Exact renderer draws (VideoMeState in source/src/video_me.h):
# counters kind 11, with the cause and address of a Media Engine exception.
EXACT_RENDERER = [
    "not-started", "media-engine", "cpu-no-module", "cpu-no-driver",
    "cpu-no-answer", "cpu-stopped", "cpu-fault", "cpu-too-slow",
    "cpu-no-memory",
]
LINK_EVENTS = {
    "link-command", "link-state", "link-pause-end", "link-error",
    "link-counters", "menu-presence", "power-suspend", "power-resume",
}
# Name-probe records (RUNTIME_LOG_EVENT_NAME_PROBE in source/src/runtime_log.h):
# flags bits 7-6 are the part, bits 5-0 the chunk index or, in the header
# part, the kind (0 an entry, 1 the end of the probe, 2 its end after a
# failed sceIoDread). A chunk is the 12 bytes of the three values, big-endian.
NAME_PROBE_PARTS = ["header", "name", "fat", "open"]
# Header kinds that end the probe, with the PSP call whose result value0 is.
NAME_PROBE_ENDS = {1: "end dopen", 2: "stopped dread"}
# Open record i holds the results labelled [3 * i, 3 * i + 3); record 1's
# third value is unused.
NAME_PROBE_OPENS = ["name", "alias-a", "alias-b", "long-a", "long-b"]
NAME_PROBE_NOT_TRIED = 0xFFFFFFFF
NAME_PROBE_ENCODINGS = ("utf-8", "shift_jis", "gbk")
# The FAT private record: layout A has the 8.3 alias at 0 and the long name
# at 13; layout B a little-endian size word, the alias at 4 and the long
# name at 20.
FAT_LAYOUT_A_LONG_NAME = 13
FAT_LAYOUT_B_ALIAS = 4
FAT_LAYOUT_B_LONG_NAME = 20


def _crc32(data: bytes) -> int:
    """CRC-32 of data as an unsigned 32-bit value."""
    return binascii.crc32(data) & 0xFFFFFFFF


def _name(names: list[str], value: int) -> str:
    """Name of an enum value, or the number when this decoder lacks it."""
    return names[value] if value < len(names) else str(value)


def _signed(value: int) -> int:
    """Reinterpret a 32-bit field as signed (PSP error codes, audio)."""
    return value if value < 0x80000000 else value - 0x100000000


def _rom_code(identity: int) -> str:
    """Four-letter game code packed into an int, or hex if not printable."""
    raw = identity.to_bytes(4, "big")
    if all(32 <= byte < 127 for byte in raw):
        return raw.decode("ascii", "replace")
    return f"0x{identity:08X}"


def _chunk(values: list[int]) -> bytes:
    """The 12 bytes a name-probe chunk carries."""
    return b"".join(value.to_bytes(4, "big") for value in values)


def _open_result(value: int) -> int | str:
    """A name-probe sceIoOpen result: the signed fd or PSP error, or not-tried."""
    return "not-tried" if value == NAME_PROBE_NOT_TRIED else _signed(value)


def _open_text(value: int | str) -> str:
    if isinstance(value, str):
        return value
    return f"fd {value}" if value >= 0 else f"error 0x{value & 0xFFFFFFFF:08X}"


def _open_results(records: dict[int, list[int]]) -> dict[str, int | str]:
    """The labelled results of an entry's open records, keyed by index."""
    return {
        label: _open_result(value)
        for index in sorted(records)
        for label, value in zip(NAME_PROBE_OPENS[3 * index:3 * index + 3],
                                records[index])
    }


def _name_views(raw: bytes) -> dict[str, str]:
    """A name's bytes as hex and as text in each candidate encoding."""
    views = {"hex": raw.hex()}
    for encoding in NAME_PROBE_ENCODINGS:
        views[encoding] = raw.decode(encoding, "replace")
    return views


def _field(data: bytes, start: int, end: int | None = None) -> bytes:
    """The bytes of data[start:end] up to the first NUL."""
    return data[start:end].split(b"\0", 1)[0]


def parse_snapshot(data: bytes, path: str = "<memory>") -> dict[str, Any]:
    """Validate one .gbrlog file and return its header and records.

    Records from all rings are merged and sorted by time. Raises ValueError
    on a truncated, unknown or corrupt file.
    """
    if len(data) < HEADER.size:
        raise ValueError(f"{path}: truncated header")
    fields = HEADER.unpack_from(data)
    (magic, version, header_size, record_size, critical_count, main_count,
     network_count, session_id, flush_reason, model, firmware, cpu_clock,
     rom_identity, started_us, flushed_us) = fields
    if magic != MAGIC or version not in HEADER_SIZES:
        raise ValueError(f"{path}: unsupported log format")
    # Version 1 files had no critical ring.
    if version == 1:
        critical_count = 0
    if header_size != HEADER_SIZES[version] or record_size != RECORD.size:
        raise ValueError(f"{path}: incompatible record layout")
    expected = header_size + (main_count + network_count + critical_count) * record_size
    if len(data) != expected:
        raise ValueError(f"{path}: expected {expected} bytes, found {len(data)}")
    # The header CRC covers every header byte before it.
    payload_crc, header_crc = struct.unpack_from(">II", data, header_size - 8)
    if _crc32(data[:header_size - 4]) != header_crc:
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
            "version": version,
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
        "build_id": (data[BUILD_ID].split(b"\0", 1)[0].decode("ascii", "replace")
                     if version >= 6 else ""),
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
    """Render a record's flags and values according to its event type."""
    event = record["event"]
    flags = record["flags"]
    value0, value1, value2 = record["values"]
    if event == "rom-open":
        return (
            f"code={_rom_code(value0)} revision={flags} size={value1} "
            f"core={_name(CPU_CORES, value2)}"
        )
    if event == "log-flush":
        return f"reason={FLUSH_REASONS.get(value0, value0)} main={value1} network={value2}"
    if event == "name-probe":
        part, index = flags >> 6, flags & 0x3F
        if part == 0 and index == 0:
            return f"entry={value0} name-bytes={value1} fat-bytes={value2}"
        if part == 0 and index in NAME_PROBE_ENDS:
            return (
                f"{NAME_PROBE_ENDS[index]}={_signed(value0)}/0x{value0:08X} "
                f"seen={value1} logged={value2}"
            )
        if part == 3:
            return "open " + " ".join(
                f"{label}={_open_text(value)}"
                for label, value in _open_results({index: record["values"]}).items()
            )
        return f"{NAME_PROBE_PARTS[part]} chunk={index} bytes={_chunk(record['values']).hex()}"
    # Earlier versions belong to a retired link design; show raw values.
    if event not in LINK_EVENTS or record.get("version", 5) < 5:
        return f"flags=0x{flags:02X} values={value0}/{value1}/{value2}"
    if event == "link-command":
        return f"role={'host' if flags else 'join'} game={_rom_code(value0)}"
    if event == "link-state":
        return (
            f"role={'host' if flags else 'join'} "
            f"status={_name(LINK_STATUS, value0)} mode={_name(LINK_MODES, value1)} "
            f"adhoc={_name(ADHOC_STATES, value2)}"
        )
    if event == "link-pause-end":
        return (
            f"mode={_name(LINK_MODES, flags)} wait={value0 / 1000:.3f}ms "
            f"status={_name(LINK_STATUS, value1)} backlog={value2}"
        )
    if event == "link-error":
        return (
            f"end={_name(LINK_ENDS, flags)} status={_name(LINK_STATUS, value0)} "
            f"adhoc-error={_signed(value1)}/0x{value1:08X} "
            f"adhoc-phase={_name(ADHOC_PHASES, value2)}"
        )
    if event == "link-counters" and flags == 11:
        return (
            f"exact-renderer={_name(EXACT_RENDERER, value0)} "
            f"me-fault-cause=0x{value1:08X} me-fault-address=0x{value2:08X}"
        )
    if event == "link-counters" and flags == 12:
        return (
            f"cpu-core={_name(CPU_CORES, value0)} blocks-compiled={value1} "
            f"code-flushes={value2}"
        )
    if event == "link-counters":
        return f"{LINK_COUNTERS.get(flags, f'counters-{flags}')}={value0}/{value1}/{value2}"
    if event == "menu-presence":
        return (
            f"gameplay={bool(value0)} link={_name(LINK_STATUS, value1)} "
            f"adhoc={_name(ADHOC_STATES, value2)}"
        )
    if event == "power-suspend":
        return f"link={_name(LINK_STATUS, value0)} adhoc={_name(ADHOC_STATES, value1)} sleep={value2}"
    return (
        f"audio={_signed(value0)} link={_name(LINK_STATUS, value1)} "
        f"adhoc={_name(ADHOC_STATES, value2)}"
    )


def _probe_bytes(chunks: dict[int, bytes], length: int | None) -> tuple[bytes, bool]:
    """Join a part's chunks, zero-filling missing ones, trimmed to length.

    Without the header, length is unknown and every chunk seen is kept.
    Returns the bytes and whether every chunk was there.
    """
    count = (length + 11) // 12 if length is not None else max(chunks, default=-1) + 1
    data = b"".join(chunks.get(index, bytes(12)) for index in range(count))
    complete = all(index in chunks for index in range(count))
    return (data[:length] if length is not None else data), complete


def name_probe_entries(snapshot: dict[str, Any]) -> list[dict[str, Any]]:
    """Rebuild the entries of the file-name probe from a snapshot's records.

    Main-ring name-probe records are read in ordinal order: each entry header
    starts an entry, which collects the chunks and open results after it.
    An entry is "complete": False when the ring overwrote any of its records
    (its header too: then its index and lengths are None).
    """
    records = sorted(
        (record for record in snapshot["records"]
         if record["event"] == "name-probe" and record["source"] == "main"),
        key=lambda record: record["ordinal"],
    )
    raw_entries: list[dict[str, Any]] = []
    current = None
    for record in records:
        part, index = record["flags"] >> 6, record["flags"] & 0x3F
        values = record["values"]
        if part == 0:
            current = None
            if index == 0:
                current = {"index": values[0], "name_length": values[1],
                           "fat_length": values[2], "name": {}, "fat": {},
                           "open": {}}
                raw_entries.append(current)
            continue
        if current is None:
            current = {"index": None, "name_length": None, "fat_length": None,
                       "name": {}, "fat": {}, "open": {}}
            raw_entries.append(current)
        if part == 3:
            current["open"][index] = values
        else:
            current[NAME_PROBE_PARTS[part]][index] = _chunk(values)

    entries = []
    for raw in raw_entries:
        name, name_complete = _probe_bytes(raw["name"], raw["name_length"])
        fat, fat_complete = _probe_bytes(raw["fat"], raw["fat_length"])
        opened = raw["open"]
        entries.append({
            "index": raw["index"],
            "complete": (raw["index"] is not None and name_complete
                         and fat_complete and {0, 1} <= opened.keys()),
            "name": _name_views(name),
            "fat_hex": fat.hex(),
            "layout_a": {
                "alias": _name_views(_field(fat, 0, FAT_LAYOUT_A_LONG_NAME)),
                "long_name": _name_views(_field(fat, FAT_LAYOUT_A_LONG_NAME)),
            },
            "layout_b": {
                "size": (int.from_bytes(fat[:4], "little")
                         if len(fat) >= 4 else None),
                "alias": _name_views(
                    _field(fat, FAT_LAYOUT_B_ALIAS, FAT_LAYOUT_B_LONG_NAME)),
                "long_name": _name_views(_field(fat, FAT_LAYOUT_B_LONG_NAME)),
            },
            "open": _open_results(opened) if opened else None,
        })
    return entries


def _print_name_views(label: str, views: dict[str, str]) -> None:
    print(
        f"      {label:16} {views['hex'] or '-'} "
        + " ".join(f"{encoding}={views[encoding]!r}" for encoding in NAME_PROBE_ENCODINGS)
    )


def print_name_probe(entries: list[dict[str, Any]]) -> None:
    """Print the rebuilt name-probe entries after a snapshot's records."""
    for entry in entries:
        index = "?" if entry["index"] is None else entry["index"]
        print(f"  name-probe entry {index}{'' if entry['complete'] else ' (incomplete)'}")
        _print_name_views("d_name", entry["name"])
        _print_name_views("A alias@0", entry["layout_a"]["alias"])
        _print_name_views("A long@13", entry["layout_a"]["long_name"])
        print(f"      {'B size@0':16} {entry['layout_b']['size']}")
        _print_name_views("B alias@4", entry["layout_b"]["alias"])
        _print_name_views("B long@20", entry["layout_b"]["long_name"])
        if entry["open"] is not None:
            print("      open             " + " ".join(
                f"{label}={_open_text(value)}" for label, value in entry["open"].items()))


def iter_log_paths(arguments: Iterable[str]) -> list[Path]:
    """Expand directories to their session-*.gbrlog files, in name order."""
    paths: list[Path] = []
    for argument in arguments:
        path = Path(argument)
        if path.is_dir():
            paths.extend(sorted(path.glob("session-*.gbrlog")))
        else:
            paths.append(path)
    return paths


def header_line(snapshot: dict[str, Any]) -> str:
    """The first line printed for a snapshot; build= only from version 6."""
    line = (
        f"{snapshot['path']}: session={snapshot['session_id']:08X} "
        f"flush={snapshot['flush_reason']} PSP={snapshot['psp_model']} "
        f"clock={snapshot['cpu_clock_mhz']}MHz ROM={snapshot['rom_identity']}"
    )
    return line + (f" build={snapshot['build_id']}" if snapshot["build_id"] else "")


def print_snapshot(snapshot: dict[str, Any]) -> None:
    """Print one snapshot as text: its header line, every record, then the
    rebuilt name-probe entries."""
    print(header_line(snapshot))
    for record in snapshot["records"]:
        elapsed = record["elapsed_us"]
        time_text = "unknown" if elapsed is None else f"+{elapsed / 1_000_000:9.3f}s"
        print(
            f"  {time_text} {record['source']:7} "
            f"{record['event']:23} {describe(record)}"
        )
    print_name_probe(name_probe_entries(snapshot))


def print_summary(snapshot: dict[str, Any]) -> None:
    """Print record counts and only the link session events.

    "retained" is records kept per ring over the highest ordinal seen,
    which shows how many older records the ring overwrote.
    """
    records = snapshot["records"]
    counts = Counter(record["event"] for record in records)
    retained = Counter(record["source"] for record in records)
    latest = {}

    for record in records:
        latest[record["source"]] = max(
            latest.get(record["source"], 0), record["ordinal"]
        )
    print(header_line(snapshot))
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
            "link-command", "link-state", "link-error", "link-counters",
        }
    ]
    for record in selected:
        elapsed = record["elapsed_us"]
        time_text = "unknown" if elapsed is None else f"+{elapsed / 1_000_000:.3f}s"
        print(
            f"  {time_text} {record['source']:8} "
            f"{record['event']:23} {describe(record)}"
        )


def main() -> int:
    """Decode each .gbrlog given as text, a summary or JSON."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", help=".gbrlog files or a logs directory")
    output = parser.add_mutually_exclusive_group()
    output.add_argument("--json", action="store_true", help="write machine-readable JSON")
    output.add_argument("--summary", action="store_true", help="write a concise diagnostic summary")
    args = parser.parse_args()
    # Decoded file names may not fit the console's encoding.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="backslashreplace")
    snapshots = [
        parse_snapshot(path.read_bytes(), str(path))
        for path in iter_log_paths(args.paths)
    ]
    if args.json:
        for snapshot in snapshots:
            snapshot["name_probe"] = name_probe_entries(snapshot)
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
