#!/usr/bin/env python3
# GBA Rush PSP, Copyright (C) 2026 Piero Carrieri, GPL-2.0-or-later.

"""Runs a GBA ROM on the PSP build in PPSSPP's headless runner and saves
the GBA frame it presents, after some seconds, as a binary PPM.

  ppsspp_frame.py [--core classic|new] [--bios FILE] [--seconds N]
                  EBOOT ROM OUT.ppm

PPSSPPHeadless saves a picture only when the program asks for one, and
its debugger's gpu.buffer.screenshot needs the GE to be paused, not the
CPU. So this starts it with the debugger (--debugger=PORT), lets it run
for N seconds of wall time (60 by default), pauses the CPU and reads the
GBA texture from VRAM (memory.read): the 240x160 picture, RGBA5551 rows
of 256 pixels at 04088000h (source/src/psp_video.c), that every present
scales onto the PSP screen, so the screen scale does not matter. Then it
stops PPSSPP. The run's files are in OUT.d/, set up as
scripts/check-eboot.sh does: a lone roms/game.gba starts by itself, the
BIOS (the built-in one without --bios) is linked as gba_bios.bin, and
tempgba.cfg (tests/eboot/global_config.c) picks the core.
PPSSPP_HEADLESS selects the runner (default ext/ppsspp/build).
"""

import argparse
import base64
import contextlib
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import time

TEXTURE = 0x04088000
STRIDE, WIDTH, HEIGHT = 256, 240, 160


def websocket(port, ppsspp):
    while True:
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=120)
            break
        except OSError:
            if ppsspp.poll() is not None:
                raise RuntimeError("PPSSPP ended before its debugger opened")
            time.sleep(0.2)
    key = base64.b64encode(os.urandom(16)).decode()
    sock.sendall(("GET /debugger HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                  "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                  f"Sec-WebSocket-Key: {key}\r\n"
                  "Sec-WebSocket-Version: 13\r\n\r\n").encode())
    reply = b""
    while b"\r\n\r\n" not in reply:
        reply += exactly(sock, 1)
    if b" 101 " not in reply.split(b"\r\n")[0]:
        raise RuntimeError(f"debugger refused: {reply[:80]!r}")
    return sock


def send(sock, message):
    """A masked text frame, as a client must send."""
    data = json.dumps(message).encode()
    mask = os.urandom(4)
    head = b"\x81" + (bytes([0x80 | len(data)]) if len(data) < 126 else
                      b"\xfe" + struct.pack(">H", len(data)))
    sock.sendall(head + mask + bytes(b ^ mask[i % 4]
                                     for i, b in enumerate(data)))


def exactly(sock, count):
    data = b""
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise RuntimeError("debugger closed the connection")
        data += chunk
    return data


def receive(sock):
    """The next message, its fragments joined; control frames skipped."""
    message = b""
    while True:
        first, second = exactly(sock, 2)
        length = second & 0x7F
        if length == 126:
            length, = struct.unpack(">H", exactly(sock, 2))
        elif length == 127:
            length, = struct.unpack(">Q", exactly(sock, 8))
        payload = exactly(sock, length)
        if first & 0x0F >= 8:
            continue
        message += payload
        if first & 0x80:
            return json.loads(message)


def wait_event(sock, event):
    while True:
        message = receive(sock)
        if message.get("event") == "error":
            raise RuntimeError(message.get("message"))
        if message.get("event") == event:
            return message


def ppm(texture):
    pixels = bytearray()
    for y in range(HEIGHT):
        for x in range(WIDTH):
            color, = struct.unpack_from("<H", texture, 2 * (y * STRIDE + x))
            pixels += bytes(((color & 31) << 3, (color >> 5 & 31) << 3,
                             (color >> 10 & 31) << 3))
    return b"P6\n%d %d\n255\n" % (WIDTH, HEIGHT) + pixels


def setup(args, root, work):
    shutil.rmtree(work, ignore_errors=True)
    for sub in ("roms", "save", "state", "cfg", "snap", "cheat", "boxart"):
        os.makedirs(f"{work}/{sub}")
    shutil.copy(args.eboot, f"{work}/EBOOT.PBP")
    prx = os.path.join(os.path.dirname(args.eboot), "media_engine.prx")
    if os.path.exists(prx):
        shutil.copy(prx, work)
    with open(f"{work}/dir.ini", "w") as f:
        for name, sub in (("rom", "roms"), ("save", "save"),
                          ("save_state", "state"), ("game_config", "cfg"),
                          ("snapshot", "snap"), ("cheat", "cheat"),
                          ("boxart", "boxart"), ("folder_boxart", "boxart")):
            f.write(f"{name}_directory = {sub}\n")
    if args.bios:
        os.symlink(os.path.abspath(args.bios), f"{work}/gba_bios.bin")
    shutil.copy(args.rom, f"{work}/roms/game.gba")
    subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O2",
                    f"-I{root}/source/src",
                    f"{root}/tests/eboot/global_config.c",
                    f"{root}/source/src/config_model.c",
                    "-o", f"{work}/global_config"], check=True)
    subprocess.run([f"{work}/global_config",
                    "1" if args.core == "new" else "0",
                    f"{work}/tempgba.cfg"], check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--core", choices=("classic", "new"),
                        default="classic")
    parser.add_argument("--bios")
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("eboot")
    parser.add_argument("rom")
    parser.add_argument("out")
    args = parser.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    headless = os.environ.get("PPSSPP_HEADLESS",
                              f"{root}/ext/ppsspp/build/PPSSPPHeadless")
    work = args.out + ".d"
    # A failed run must not leave an earlier frame there to be read.
    with contextlib.suppress(FileNotFoundError):
        os.remove(args.out)
    setup(args, root, work)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    with open(f"{work}/ppsspp.log", "w") as log:
        ppsspp = subprocess.Popen(
            [headless, f"--debugger={port}",
             f"--timeout={args.seconds + 600}", f"{work}/EBOOT.PBP"],
            stdout=log, stderr=subprocess.STDOUT)
    try:
        sock = websocket(port, ppsspp)
        send(sock, {"event": "cpu.resume"})
        time.sleep(args.seconds)
        if ppsspp.poll() is not None:
            raise RuntimeError("PPSSPP ended during the run")
        send(sock, {"event": "cpu.stepping"})
        wait_event(sock, "cpu.stepping")
        send(sock, {"event": "memory.read", "address": TEXTURE,
                    "size": STRIDE * 2 * HEIGHT})
        texture = base64.b64decode(wait_event(sock, "memory.read")["base64"])
    finally:
        ppsspp.kill()
        ppsspp.wait()
    with open(args.out, "wb") as f:
        f.write(ppm(texture))
    print(f"{args.out}: GBA frame after {args.seconds:g} s on the"
          f" {args.core} core")
    return 0


if __name__ == "__main__":
    sys.exit(main())
