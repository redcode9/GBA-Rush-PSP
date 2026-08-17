# Copyright (C) 2026 JxP
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Audio playback engine using miniaudio.

Decodes entire files to memory for instant seeking.
Thread-safe flag-based synchronization with the UI layer.

CRITICAL DESIGN RULE: The audio callback runs in miniaudio's native thread.
We NEVER call Tkinter or any GUI code from that thread. Instead, we set simple
flags (_finished_naturally, _playback_error, _init_failed) that the main thread
polls via _poll_ui().
"""

import array
import threading
from pathlib import Path

import logging

logger = logging.getLogger("tempgba_builder")


class AudioPlayer:
    """
    Audio player using miniaudio for reliable playback and seeking.

    Decodes the entire file to memory on load for instant seeking.
    No subprocesses during playback. No temp files.

    Thread safety: The audio callback runs in miniaudio's native thread.
    We NEVER call Tkinter or any GUI code from that thread. Instead, we
    set a simple flag (_finished_naturally) that the main thread polls.
    """

    def __init__(self):
        self._source_path = None
        self._duration = 0.0
        self._sample_rate = 44100
        self._channels = 2
        self._samples = None       # array.array of decoded PCM samples
        self._total_frames = 0
        self._position = 0.0       # current position in seconds
        self._playing = False
        self._paused = False
        self._segment_end = 0.0
        self._device = None
        self._playback_thread = None
        self._read_cursor = 0      # current frame position in _samples
        self._finished_naturally = False  # Set by audio thread, read by main thread
        self._playback_error = None
        self._init_failed = False  # True if device init fails immediately

    def load(self, path: Path) -> float:
        """
        Load and decode audio file to memory, return duration in seconds.

        Always decodes to 44100Hz stereo S16 for consistency.
        """
        import miniaudio

        if not path.exists():
            raise RuntimeError(f"Audio file not found: {path}")

        # Decode entire file to memory
        decoded = miniaudio.decode_file(
            str(path),
            output_format=miniaudio.SampleFormat.SIGNED16,
            nchannels=2,
            sample_rate=44100
        )

        self._samples = decoded.samples
        self._sample_rate = decoded.sample_rate
        self._channels = decoded.nchannels
        self._total_frames = len(decoded.samples) // self._channels
        self._duration = self._total_frames / self._sample_rate
        self._source_path = path
        self._position = 0.0
        self._read_cursor = 0
        self._finished_naturally = False

        return self._duration

    def _get_frames(self, frame_count: int):
        """
        Get frame_count frames starting from _read_cursor.

        Returns bytes (or None if EOF or segment end reached).
        """
        if self._samples is None:
            return None

        start = self._read_cursor * self._channels
        end = start + (frame_count * self._channels)

        # Check segment end
        if self._segment_end > 0:
            end_frame = int(self._segment_end * self._sample_rate)
            if self._read_cursor >= end_frame:
                return None  # Reached segment end
            max_end = end_frame * self._channels
            end = min(end, max_end)

        # Check file end
        total_samples = len(self._samples)
        end = min(end, total_samples)

        if start >= end:
            return None

        frames = self._samples[start:end]
        self._read_cursor += (end - start) // self._channels

        # Convert array.array to bytes
        if isinstance(frames, array.array):
            return frames.tobytes()
        return frames

    def _audio_callback(self):
        """
        Generator that feeds audio data to miniaudio PlaybackDevice.

        CRITICAL: This runs in miniaudio's audio thread. Do NOT call any
        GUI code, Tkinter methods, or callbacks from here. Only set flags.
        """
        import miniaudio

        # First yield primes the generator (called by next())
        framecount = yield b"\x00" * (1024 * self._channels * 2)

        while self._playing:
            if self._paused:
                framecount = yield b"\x00" * (framecount * self._channels * 2)
                continue

            frames = self._get_frames(framecount)
            if frames is None:
                # End of file or segment — set flag, exit generator
                self._finished_naturally = True
                self._playing = False
                break

            framecount = yield frames

    def play(self, start_sec: float = 0, segment_end: float = 0):
        """
        Play from start_sec. If segment_end > 0, stop at that position.

        Does NOT take a callback — use _poll_ui() on the main thread to
        detect playback end via the finished_naturally property.
        """
        import miniaudio

        if self._source_path is None or self._samples is None:
            raise RuntimeError("No audio loaded")

        if start_sec >= self._duration:
            raise RuntimeError(
                f"Start time {start_sec:.2f}s is at or beyond track duration "
                f"{self._duration:.2f}s"
            )
        if segment_end > 0 and segment_end <= start_sec:
            raise RuntimeError("Segment end must be after start")

        self.stop()  # stop any existing playback

        self._playing = True
        self._paused = False
        self._finished_naturally = False
        self._segment_end = segment_end

        # Set read cursor to start position
        self._read_cursor = int(start_sec * self._sample_rate)
        self._position = start_sec

        # Create and start playback device
        self._device = miniaudio.PlaybackDevice(
            output_format=miniaudio.SampleFormat.SIGNED16,
            nchannels=self._channels,
            sample_rate=self._sample_rate,
            buffersize_msec=100
        )

        self._playback_thread = threading.Thread(target=self._run_playback, daemon=True)
        self._playback_thread.start()

    def _run_playback(self):
        """Run the playback device. Called in a background thread."""
        try:
            callback_gen = self._audio_callback()
            next(callback_gen)  # Prime the generator (first yield)
            self._device.start(callback_gen)
        except Exception as e:
            self._playing = False
            self._init_failed = True
            self._playback_error = str(e)
            logger.exception("Audio playback thread failed")

    def seek(self, sec: float, segment_end: float = 0):
        """Seek to position and auto-play."""
        if self._source_path is None:
            return

        self.stop()
        self._position = max(0, min(sec, self._duration - 0.01))
        self.play(self._position, segment_end)

    def pause(self):
        """Pause playback."""
        if self._playing and not self._paused:
            self._paused = True
            self._position = self._read_cursor / self._sample_rate

    def resume(self):
        """Resume from pause."""
        if self._paused:
            self._paused = False

    def stop(self):
        """Stop playback and release the audio device."""
        self._playing = False
        self._paused = False
        self._segment_end = 0

        if self._device:
            try:
                self._device.stop()
                self._device.close()
            except Exception:
                pass
            self._device = None

        if self._playback_thread:
            self._playback_thread.join(timeout=1.0)
            self._playback_thread = None

        self._read_cursor = 0
        self._position = 0

    def cleanup(self):
        """Full cleanup: stop playback and release decoded samples."""
        self.stop()
        self._source_path = None
        self._samples = None

    @property
    def position(self) -> float:
        """Current absolute position in seconds."""
        if self._paused:
            return self._position
        if self._playing:
            return self._read_cursor / self._sample_rate
        return self._position

    @property
    def duration(self) -> float:
        """Total track duration in seconds."""
        return self._duration

    @property
    def is_playing(self) -> bool:
        """True if currently playing (not paused)."""
        return self._playing and not self._paused

    @property
    def finished_naturally(self) -> bool:
        """
        True if playback ended due to EOF/segment end.

        Set by audio thread; must be cleared by main thread after handling.
        """
        return self._finished_naturally

    def clear_finished_flag(self):
        """Clear the finished flag. Call after handling it on main thread."""
        self._finished_naturally = False