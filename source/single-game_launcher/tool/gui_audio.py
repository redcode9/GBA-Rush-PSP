# Copyright (C) 2026 JxP
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Audio Tab Mixin for BuilderApp.

Provides:
  - Audio source selection (Local File vs YouTube URL)
  - Segment selection with Start/End time entries
  - Interactive timeline canvas (click to seek, drag with debounce)
  - Playback controls (Play, Play Segment, Pause, Stop)
  - Loop support with automatic segment restart
  - YouTube download with async threading and stale-request filtering
"""

import threading
import time
from pathlib import Path

import customtkinter as ctk
import constants as C

import logging

logger = logging.getLogger("tempgba_builder")

from gui_utils import _format_time, _format_time_hhmmss, _strip_quotes


class AudioTabMixin:
    """Mixin providing the Audio tab and playback controls."""

    # =====================================================================
    # Audio Tab UI Construction
    # =====================================================================

    def _setup_audio_tab(self):
        """Build all widgets for the 'Audio' tab."""
        frame = ctk.CTkFrame(self.tab_audio, fg_color="transparent")
        frame.pack(fill="both", expand=True, padx=10, pady=10)

        # --- Audio Source Selection ---
        ctk.CTkLabel(frame, text="Audio Source", font=ctk.CTkFont(weight="bold")).pack(anchor="w", pady=(10, 2))

        self.audio_type_var = ctk.StringVar(value="local")
        type_frame = ctk.CTkFrame(frame, fg_color="transparent")
        type_frame.pack(fill="x", pady=5)

        ctk.CTkRadioButton(type_frame, text="Local File", variable=self.audio_type_var,
                          value="local", command=self._on_audio_type_change).pack(side="left", padx=(0, 20))
        ctk.CTkRadioButton(type_frame, text="YouTube URL", variable=self.audio_type_var,
                          value="youtube", command=self._on_audio_type_change).pack(side="left")

        # Container that holds either local or YouTube input widgets
        self.audio_input_container = ctk.CTkFrame(frame, fg_color="transparent")
        self.audio_input_container.pack(fill="x", pady=5)

        # Local file input (default visible)
        self.local_audio_frame = ctk.CTkFrame(self.audio_input_container, fg_color="transparent")
        self.local_audio_frame.pack(fill="x")

        self.audio_entry = ctk.CTkEntry(self.local_audio_frame, placeholder_text="Path to audio file")
        self.audio_entry.pack(side="left", fill="x", expand=True)
        self.audio_entry.bind("<KeyRelease>", self._on_any_change)
        ctk.CTkButton(
            self.local_audio_frame, text="Browse", width=80,
            command=lambda: self._browse_file(
                self.audio_entry,
                [("Audio", "*.mp3 *.wav *.flac *.ogg *.m4a")],
                lambda p: self._load_audio_file(p, reset_times=True)
            )
        ).pack(side="left", padx=(5, 0))

        # YouTube URL input (hidden initially)
        self.youtube_frame = ctk.CTkFrame(self.audio_input_container, fg_color="transparent")

        self.youtube_entry = ctk.CTkEntry(self.youtube_frame,
                                          placeholder_text="https://www.youtube.com/watch?v=...")
        self.youtube_entry.pack(side="left", fill="x", expand=True)
        self.youtube_entry.bind("<KeyRelease>", self._on_any_change)
        ctk.CTkButton(self.youtube_frame, text="Load URL", width=80,
                     command=self._load_youtube_url).pack(side="left", padx=(5, 0))

        # --- Loop Checkbox ---
        self.loop_var = ctk.BooleanVar(value=False)
        loop_cb = ctk.CTkCheckBox(frame, text="Loop audio", variable=self.loop_var, command=self._on_any_change)
        loop_cb.pack(anchor="w", pady=10)

        # --- Start / End Time Selection ---
        time_frame = ctk.CTkFrame(frame, fg_color=C.COLOR_BG_DARK, corner_radius=8)
        time_frame.pack(fill="x", pady=5)

        ctk.CTkLabel(time_frame, text="Segment Selection (HH:MM:SS)",
                     font=ctk.CTkFont(weight="bold")).pack(anchor="w", padx=10, pady=(10, 5))

        time_inputs = ctk.CTkFrame(time_frame, fg_color="transparent")
        time_inputs.pack(fill="x", padx=10, pady=5)

        ctk.CTkLabel(time_inputs, text="Start:").pack(side="left")
        self.start_time_entry = ctk.CTkEntry(time_inputs, width=80, placeholder_text="0:00:00")
        self.start_time_entry.pack(side="left", padx=(5, 15))
        self.start_time_entry.insert(0, "0:00:00")
        self.start_time_entry.bind("<KeyRelease>", self._on_any_change)

        ctk.CTkLabel(time_inputs, text="End:").pack(side="left")
        self.end_time_entry = ctk.CTkEntry(time_inputs, width=80, placeholder_text="auto")
        self.end_time_entry.pack(side="left", padx=(5, 15))
        self.end_time_entry.bind("<KeyRelease>", self._on_any_change)

        ctk.CTkButton(time_inputs, text="Set Start", width=80,
                     command=self._set_start_from_playhead).pack(side="left", padx=(5, 0))
        ctk.CTkButton(time_inputs, text="Set End", width=80,
                     command=self._set_end_from_playhead).pack(side="left", padx=(5, 0))

        ctk.CTkLabel(time_frame, text="Leave End blank for auto-trim to ~500KB limit.",
                    font=ctk.CTkFont(size=10), text_color=C.COLOR_TEXT_MUTED).pack(anchor="w", padx=10, pady=(0, 10))

        # --- Timeline Canvas ---
        timeline_frame = ctk.CTkFrame(frame, fg_color=C.COLOR_BG_DARKEST, corner_radius=8)
        timeline_frame.pack(fill="x", pady=5)

        ctk.CTkLabel(timeline_frame, text="Timeline", font=ctk.CTkFont(weight="bold")).pack(anchor="w", padx=10, pady=(10, 5))
        self.timeline_canvas = ctk.CTkCanvas(timeline_frame, height=50,
                                             bg=C.COLOR_BG_DARKEST, highlightthickness=0)
        self.timeline_canvas.pack(fill="x", padx=10, pady=5)
        self.timeline_canvas.bind("<Button-1>", self._on_timeline_click)
        self.timeline_canvas.bind("<B1-Motion>", self._on_timeline_drag)

        time_labels = ctk.CTkFrame(timeline_frame, fg_color="transparent")
        time_labels.pack(fill="x", padx=10, pady=(0, 10))
        self.timeline_start_label = ctk.CTkLabel(time_labels, text="0:00",
                                                 font=ctk.CTkFont(size=10), text_color=C.COLOR_TEXT_MUTED)
        self.timeline_start_label.pack(side="left")
        self.timeline_end_label = ctk.CTkLabel(time_labels, text="0:00",
                                               font=ctk.CTkFont(size=10), text_color=C.COLOR_TEXT_MUTED)
        self.timeline_end_label.pack(side="right")

        # --- Player Controls ---
        player_frame = ctk.CTkFrame(frame, fg_color=C.COLOR_BG_DARK, corner_radius=8)
        player_frame.pack(fill="x", pady=10)

        ctk.CTkLabel(player_frame, text="Preview", font=ctk.CTkFont(weight="bold")).pack(anchor="w", padx=10, pady=(10, 5))

        ctrl = ctk.CTkFrame(player_frame, fg_color="transparent")
        ctrl.pack(fill="x", padx=10, pady=5)

        self.play_btn = ctk.CTkButton(ctrl, text="Play", width=80, command=self._play_preview)
        self.play_btn.pack(side="left", padx=(0, 5))

        self.play_segment_btn = ctk.CTkButton(ctrl, text="Play Segment", width=100, command=self._play_segment)
        self.play_segment_btn.pack(side="left", padx=(0, 5))

        self.stop_btn = ctk.CTkButton(ctrl, text="Stop", width=80, command=self._stop_preview)
        self.stop_btn.pack(side="left")

        self.preview_status = ctk.CTkLabel(ctrl, text="Ready", text_color=C.COLOR_TEXT_MUTED)
        self.preview_status.pack(side="left", padx=(20, 0))

        self.time_label = ctk.CTkLabel(ctrl, text="0:00:00 / 0:00:00", width=120)
        self.time_label.pack(side="right")

        ctk.CTkLabel(frame, text="Note: PSP SND0.AT3 has a hard 500KB size limit (~30 seconds of stereo ATRAC3). "
                                  "Longer tracks will be auto-trimmed.",
                    font=ctk.CTkFont(size=10), text_color=C.COLOR_TEXT_MUTED).pack(anchor="w", pady=(5, 0))

        # Check miniaudio availability at startup
        try:
            import miniaudio
            self._audio_available = True
        except ImportError as e:
            self._audio_available = False
            print(f"Audio preview unavailable: {e}")

    def _check_audio_available(self):
        """
        Return True if audio preview is available.

        If miniaudio failed to import or no audio device exists, shows an
        error dialog and returns False so the caller can abort playback.
        """
        if not getattr(self, '_audio_available', False):
            self._show_error("Audio Unavailable",
                "miniaudio is not installed or no audio device was detected.\n"
                "You can still configure audio and build — preview just won't play.")
            return False
        return True

    def _parse_time(self, text: str) -> float:
        """
        Parse HH:MM:SS, MM:SS, or raw seconds to float seconds.

        Returns -1 on invalid input so callers can detect parse failures.
        """
        text = text.strip()
        if not text:
            return 0
        parts = text.split(":")
        try:
            if len(parts) == 3:
                h, m, s = int(parts[0]), int(parts[1]), float(parts[2])
                if h < 0 or m < 0 or m > 59 or s < 0 or s >= 60:
                    return -1
                return h * 3600 + m * 60 + s
            elif len(parts) == 2:
                m, s = int(parts[0]), float(parts[1])
                if m < 0 or s < 0 or s >= 60:
                    return -1
                return m * 60 + s
            else:
                s = float(parts[0])
                if s < 0:
                    return -1
                return s
        except ValueError:
            return -1

    def _draw_timeline(self):
        """
        Redraw the timeline canvas.

        Shows the full track as a dark bar, the selected segment in accent color,
        and a white playhead line at the current playback position.
        """
        canvas = self.timeline_canvas
        w = canvas.winfo_width()
        h = canvas.winfo_height()
        if w <= 1:
            # Canvas not ready yet — retry shortly
            self._safe_after(100, self._draw_timeline)
            return

        canvas.delete("all")

        if not self._audio_loaded or self._player.duration <= 0:
            # No audio loaded — draw empty track
            canvas.create_rectangle(2, 20, w - 2, 35, fill=C.COLOR_BG_DARK, outline="#333333", width=1)
            return

        dur = self._player.duration
        start_sec = self._parse_time(self.start_time_entry.get())
        end_text = self.end_time_entry.get().strip()
        end_sec = self._parse_time(end_text) if end_text else dur

        # Clamp to valid range
        start_sec = max(0, min(start_sec, dur))
        end_sec = max(start_sec, min(end_sec, dur))

        # Background track
        canvas.create_rectangle(2, 20, w - 2, 35, fill=C.COLOR_BG_DARK, outline="#333333", width=1)
        # Selected segment highlight
        x1 = 2 + (start_sec / dur) * (w - 4)
        x2 = 2 + (end_sec / dur) * (w - 4)
        canvas.create_rectangle(x1, 20, x2, 35, fill=C.COLOR_ACCENT, outline="", width=0)

        # Playhead
        pos = self._player.position
        playhead_x = 2 + (pos / dur) * (w - 4) if self._audio_loaded else 2
        playhead_x = max(2, min(playhead_x, w - 2))
        canvas.create_line(playhead_x, 15, playhead_x, 40, fill="#ffffff", width=2)

        self.timeline_start_label.configure(text=_format_time(0))
        self.timeline_end_label.configure(text=_format_time(dur))

    def _execute_timeline_seek(self, sec: float):
        """
        Perform the actual audio seek after a click or drag debounce.

        Respects the current segment end: if seeking inside the segment,
        the segment end is preserved; if seeking past it, the segment is cleared.
        """
        if self._shutdown:
            return
        self._drag_debounce_id = None

        current_seg_end = getattr(self, '_segment_end', 0)
        seg_end_to_pass = 0

        if current_seg_end > 0:
            if sec < current_seg_end:
                seg_end_to_pass = current_seg_end
            else:
                self._segment_end = 0

        try:
            self._player.seek(sec, segment_end=seg_end_to_pass)
        except Exception as e:
            self._show_error("Seek failed", str(e))
            return
        self._draw_timeline()
        self._update_time_label()
        self.preview_status.configure(text="Playing", text_color=C.COLOR_SUCCESS)
        self.play_btn.configure(text="Pause", command=self._pause_preview)
        self._start_ui_poll()

    def _update_time_label_at(self, sec: float):
        """Update the time label to show a specific position (used during drag preview)."""
        dur = self._player.duration
        self.time_label.configure(text=f"{_format_time_hhmmss(sec)} / {_format_time_hhmmss(dur)}")

    def _on_timeline_click(self, event):
        """Seek to the clicked position on the timeline."""
        if not self._check_audio_available():
            return
        if not self._audio_loaded or self._player.duration <= 0:
            return
        # Cancel any pending drag debounce so we don't seek twice
        if self._drag_debounce_id:
            self.after_cancel(self._drag_debounce_id)
            self._drag_debounce_id = None
        w = self.timeline_canvas.winfo_width()
        ratio = max(0, min(1, (event.x - 2) / (w - 4)))
        sec = ratio * self._player.duration
        self._execute_timeline_seek(sec)

    def _on_timeline_drag(self, event):
        """
        Handle timeline drag (click-and-hold mouse motion).

        Provides immediate visual feedback (orange preview playhead) but
        debounces the actual audio seek to avoid stuttering (150ms after drag stops).
        """
        if not self._check_audio_available():
            return
        if not self._audio_loaded or self._player.duration <= 0:
            return

        w = self.timeline_canvas.winfo_width()
        ratio = max(0, min(1, (event.x - 2) / (w - 4)))
        sec = ratio * self._player.duration

        # Visual feedback only: redraw timeline and show orange preview playhead
        self._draw_timeline()
        dur = self._player.duration
        playhead_x = 2 + (sec / dur) * (w - 4)
        playhead_x = max(2, min(playhead_x, w - 2))
        self.timeline_canvas.create_line(playhead_x, 15, playhead_x, 40, fill=C.COLOR_WARNING, width=2)
        self._update_time_label_at(sec)

        # Debounce actual seek: only seek 150ms after drag stops
        if self._drag_debounce_id:
            self.after_cancel(self._drag_debounce_id)
        self._drag_debounce_id = self.after(150, lambda s=sec: self._execute_timeline_seek(s))

    def _update_time_label(self):
        """Update time label from current player position."""
        pos = self._player.position
        dur = self._player.duration
        self.time_label.configure(text=f"{_format_time_hhmmss(pos)} / {_format_time_hhmmss(dur)}")

    def _set_start_from_playhead(self):
        """Set the Start time entry to the current playback position."""
        sec = self._player.position
        val = _format_time_hhmmss(sec)
        if self.audio_type_var.get() == "local":
            self._local_start = val
        else:
            self._yt_start = val
        self.start_time_entry.delete(0, "end")
        self.start_time_entry.insert(0, val)
        self._draw_timeline()
        self._on_any_change()   # mark summary dirty

    def _set_end_from_playhead(self):
        """Set the End time entry to the current playback position."""
        sec = self._player.position
        val = _format_time_hhmmss(sec)
        if self.audio_type_var.get() == "local":
            self._local_end = val
        else:
            self._yt_end = val
        self.end_time_entry.delete(0, "end")
        self.end_time_entry.insert(0, val)
        self._draw_timeline()
        self._on_any_change()   # mark summary dirty

    def _on_audio_type_change(self):
        """
        Handle switching between Local File and YouTube URL modes.

        Stops playback, persists the current source's start/end times,
        restores the other source's times, and reloads the appropriate
        cached audio file into the player.
        """
        # Always stop playback and reset segment state when switching sources
        self._player.stop()
        self._player._position = 0
        self._segment_end = 0

        if self.audio_type_var.get() == "local":
            # Switching to Local: save YouTube values, restore Local values
            self._yt_start = self.start_time_entry.get()
            self._yt_end = self.end_time_entry.get()
            self.youtube_frame.pack_forget()
            self.local_audio_frame.pack(fill="x")
            self.start_time_entry.delete(0, "end")
            self.start_time_entry.insert(0, self._local_start)
            self.end_time_entry.delete(0, "end")
            if self._local_end:
                self.end_time_entry.insert(0, self._local_end)

            # Restore local audio path from entry or cache
            typed = _strip_quotes(self.audio_entry.get().strip())
            if typed and Path(typed).exists():
                self.audio_path = Path(typed)
                self._local_audio_path = Path(typed)
            elif self._local_audio_path and self._local_audio_path.exists():
                self.audio_path = self._local_audio_path
            else:
                self.audio_path = None

            # If we have a valid local file, load it back into the player
            if self.audio_path and self.audio_path.exists():
                if self._load_player_file(str(self.audio_path)):
                    self.preview_status.configure(text="Loaded", text_color=C.COLOR_TEXT_MUTED)
                else:
                    self.preview_status.configure(text="Error", text_color=C.COLOR_ERROR)
            else:
                self._audio_loaded = False
                self.preview_status.configure(text="Ready", text_color=C.COLOR_TEXT_MUTED)
            self.play_btn.configure(text="Play", command=self._play_preview)

            # Do NOT delete YouTube cache — we want to keep it

        else:
            # Switching to YouTube: save Local values, restore YouTube values
            self._local_start = self.start_time_entry.get()
            self._local_end = self.end_time_entry.get()
            self.local_audio_frame.pack_forget()
            self.youtube_frame.pack(fill="x")
            self.start_time_entry.delete(0, "end")
            self.start_time_entry.insert(0, self._yt_start)
            self.end_time_entry.delete(0, "end")
            if self._yt_end:
                self.end_time_entry.insert(0, self._yt_end)

            # Restore YouTube audio path from cache if available
            url = self.youtube_entry.get().strip()
            if url and url in self._yt_cache and self._yt_cache[url].exists():
                self.audio_path = self._yt_cache[url]
            else:
                self.audio_path = None

            # If we have a valid cached file, load it back into the player
            if self.audio_path and self.audio_path.exists():
                if self._load_player_file(str(self.audio_path)):
                    self.preview_status.configure(text="Loaded", text_color=C.COLOR_TEXT_MUTED)
                else:
                    self.preview_status.configure(text="Error", text_color=C.COLOR_ERROR)
            else:
                self._audio_loaded = False
                self.preview_status.configure(text="Ready", text_color=C.COLOR_TEXT_MUTED)
            self.play_btn.configure(text="Play", command=self._play_preview)

            # Do NOT delete local cache — we want to keep it

        self._on_any_change()

    def _restore_audio_tab_state(self):
        """
        Restore audio source state when entering the Audio tab.

        Reloads the appropriate cached file (local or YouTube) into the player
        so the timeline and controls reflect the current selection.
        """
        self._stop_preview()

        if self.audio_type_var.get() == "local":
            typed = _strip_quotes(self.audio_entry.get().strip())
            if typed and Path(typed).exists():
                self.audio_path = Path(typed)
                self._local_audio_path = Path(typed)
            elif self._local_audio_path and self._local_audio_path.exists():
                self.audio_path = self._local_audio_path
            else:
                self.audio_path = None

            if self.audio_path and self.audio_path.exists():
                if self._load_player_file(str(self.audio_path)):
                    self.preview_status.configure(text="Loaded", text_color=C.COLOR_TEXT_MUTED)
                else:
                    self.preview_status.configure(text="Error", text_color=C.COLOR_ERROR)
            else:
                self._audio_loaded = False
                self.preview_status.configure(text="Ready", text_color=C.COLOR_TEXT_MUTED)
        else:
            url = self.youtube_entry.get().strip()
            if url and url in self._yt_cache and self._yt_cache[url].exists():
                self.audio_path = self._yt_cache[url]
            else:
                self.audio_path = None

            if self.audio_path and self.audio_path.exists():
                if self._load_player_file(str(self.audio_path)):
                    self.preview_status.configure(text="Loaded", text_color=C.COLOR_TEXT_MUTED)
                else:
                    self.preview_status.configure(text="Error", text_color=C.COLOR_ERROR)
            else:
                self._audio_loaded = False
                self.preview_status.configure(text="Ready", text_color=C.COLOR_TEXT_MUTED)

        self._draw_timeline()
        self._update_time_label()
        self.play_btn.configure(text="Play", command=self._play_preview)

    def _load_player_file(self, path: str) -> bool:
        """
        Load audio into the player without touching UI state.

        Returns True on success, False on failure. Used internally by
        restore and mode-switch logic.
        """
        try:
            self._player.stop()
            duration = self._player.load(Path(path))
            self._audio_loaded = True
            self._player._position = 0
            return True
        except Exception as e:
            self._show_error("Audio load failed", str(e))
            self._audio_loaded = False
            return False

    def _load_audio_file(self, path: str, reset_times: bool = True) -> bool:
        """
        Load audio and optionally reset start/end times.

        Only resets times if this is a genuinely new file (different path
        from the previously loaded one).
        """
        path = Path(path)

        # Only reset times if this is a genuinely new file
        is_new_file = (self.audio_path is None) or (str(self.audio_path) != str(path))

        if not self._load_player_file(str(path)):
            return False

        self.audio_path = path
        self._local_audio_path = path

        if reset_times and is_new_file:
            self._local_start = "0:00:00"
            self._local_end = ""
            self.start_time_entry.delete(0, "end")
            self.start_time_entry.insert(0, "0:00:00")
            self.end_time_entry.delete(0, "end")
            self._segment_end = 0
            self._player._segment_end = 0

        self._draw_timeline()
        self._update_time_label()
        self.preview_status.configure(text="Loaded", text_color=C.COLOR_TEXT_MUTED)
        self.play_btn.configure(text="Play", command=self._play_preview)
        self._on_any_change()   # mark summary dirty
        return True

    def _play_preview(self):
        """
        Play the full audio file from the current position.

        For YouTube mode, triggers a download if the URL hasn't been cached yet.
        """
        if not self._check_audio_available():
            return

        # Check if we need to load from current source first
        if self.audio_type_var.get() == "youtube":
            url = self.youtube_entry.get().strip()
            if not url:
                self._show_error("No URL", "Please enter a YouTube URL or switch to Local File mode.")
                return
            if not self._audio_loaded or self.audio_path is None:
                # Check if we already have this URL cached
                if url in self._yt_cache and self._yt_cache[url].exists():
                    self._load_player_file(str(self._yt_cache[url]))
                    self.audio_path = self._yt_cache[url]
                    # FALL THROUGH to play below
                else:
                    self._yt_pending_action = "play"
                    self._download_and_play_youtube()
                    return  # async download will play when ready
        elif not self._audio_loaded:
            # Local mode, nothing loaded
            path = self.audio_entry.get().strip()
            if path:
                if not self._load_audio_file(path, reset_times=False):  # preserve times
                    return
            else:
                return

        # Clear any previous segment end so full file plays
        self._segment_end = 0
        self._player._segment_end = 0

        try:
            self._player.play(start_sec=self._player.position)
        except Exception as e:
            self._show_error("Playback failed", str(e))
            self.preview_status.configure(text="Error", text_color=C.COLOR_ERROR)
            return

        self.preview_status.configure(text="Playing", text_color=C.COLOR_SUCCESS)
        self.play_btn.configure(text="Pause", command=self._pause_preview)
        self._start_ui_poll()

        # Check for immediate init failure (no audio device, bad drivers)
        self._safe_after(150, self._check_playback_init)

    def _play_segment(self):
        """
        Play only the selected time segment.

        Validates start/end times, clamps them to the track duration,
        and starts playback with a segment end bound.
        """
        if not self._check_audio_available():
            return
        if not self._audio_loaded:
            if self.audio_type_var.get() == "local":
                path = self.audio_entry.get().strip()
                if path:
                    if not self._load_audio_file(path):
                        return
                else:
                    return
            else:
                self._yt_pending_action = "play_segment"
                self._download_and_play_youtube()
                return

        start_sec = self._parse_time(self.start_time_entry.get())
        end_text = self.end_time_entry.get().strip()
        end_sec = self._parse_time(end_text) if end_text else self._player.duration

        # Validate time format
        if start_sec < 0:
            self._show_error("Invalid Start Time",
                             f"Cannot parse: '{self.start_time_entry.get().strip()}'\n"
                             "Use HH:MM:SS, MM:SS, or seconds.")
            return
        if end_text and end_sec < 0:
            self._show_error("Invalid End Time",
                             f"Cannot parse: '{end_text}'\n"
                             "Use HH:MM:SS, MM:SS, or seconds.")
            return

        # Clamp to track bounds
        start_sec = max(0, min(start_sec, self._player.duration))
        end_sec = max(start_sec, min(end_sec, self._player.duration))

        # Guard against empty or invalid segments
        if start_sec >= self._player.duration:
            self._show_error("Invalid Start Time",
                             "Start time is at or beyond the end of the track.")
            return
        if end_sec <= start_sec:
            self._show_error("Invalid Segment",
                             "End time must be greater than start time.")
            return

        self._segment_end = end_sec
        try:
            self._player.play(start_sec=start_sec, segment_end=end_sec)
        except Exception as e:
            self._show_error("Playback failed", str(e))
            self.preview_status.configure(text="Error", text_color=C.COLOR_ERROR)
            return

        self.preview_status.configure(text="Playing Segment", text_color=C.COLOR_SUCCESS)
        self.play_btn.configure(text="Pause", command=self._pause_preview)
        self._start_ui_poll()

    def _download_and_play_youtube(self):
        """
        Download YouTube audio in a background thread, then load & play.

        Uses a generation ID (_yt_download_id) to ignore stale callbacks
        from superseded requests (e.g., user clicked Load URL twice rapidly).
        """
        url = self.youtube_entry.get().strip()
        if not url:
            return

        # If same URL already cached, reuse instantly (no thread, no download)
        if url in self._yt_cache:
            cached_path = self._yt_cache[url]
            if cached_path.exists():
                self._on_youtube_loaded(cached_path, url)
                return

        self._stop_preview()
        self.preview_status.configure(text="Downloading...", text_color=C.COLOR_WARNING)
        self.update()

        # Increment generation ID so stale callbacks can be ignored
        self._yt_download_id += 1
        current_id = self._yt_download_id

        def _thread_worker():
            try:
                from convert import download_youtube
                # Use nanosecond timestamp for unique temp filename (avoids Windows lock issues)
                tmp = self._temp_dir / f"yt_{time.time_ns()}.wav"
                download_youtube(url, tmp)
                if not self._shutdown:
                    self.after(0, lambda: self._on_youtube_loaded(tmp, url, current_id))
            except Exception as e:
                if not self._shutdown:
                    err_msg = str(e)
                    self.after(0, lambda: self._on_youtube_error(err_msg, current_id))

        self._download_thread = threading.Thread(target=_thread_worker, daemon=True)
        self._download_thread.start()

    def _on_youtube_loaded(self, tmp: Path, url: str, download_id: int = None):
        """
        Main-thread callback after successful YouTube download.

        Caches the file, loads it into the player, resets times for new URLs,
        and triggers the pending playback action (play or play_segment).
        """
        if self._shutdown:
            return

        # Ignore stale downloads from superseded requests
        if download_id is not None and download_id != self._yt_download_id:
            return

        # Cache on main thread (safe, and only if not stale)
        self._yt_cache[url] = tmp

        is_new_url = (self.youtube_url != url)

        # Load into player without touching Local cache
        if not self._load_player_file(str(tmp)):
            return

        self.audio_path = tmp

        if is_new_url:
            # Reset times for a brand new URL
            self._yt_start = "0:00:00"
            self._yt_end = ""
            self.start_time_entry.delete(0, "end")
            self.start_time_entry.insert(0, "0:00:00")
            self.end_time_entry.delete(0, "end")
            self._parse_youtube_start_time(url)
        # else: preserve existing _yt_start / _yt_end, entry boxes already correct

        self.youtube_url = url
        self._player._position = 0
        self._draw_timeline()
        self._update_time_label()
        self.preview_status.configure(text="Loaded", text_color=C.COLOR_TEXT_MUTED)
        self.play_btn.configure(text="Play", command=self._play_preview)
        self._on_any_change()

        # Respect the action that triggered the download
        action = getattr(self, '_yt_pending_action', 'play')
        self._yt_pending_action = None  # clear it

        if action == "play_segment":
            self._play_segment()
        else:
            self._play_preview()

    def _on_youtube_error(self, msg: str, download_id: int = None):
        """Main-thread callback after failed YouTube download."""
        if self._shutdown:
            return

        # Ignore stale error popups from superseded requests
        if download_id is not None and download_id != self._yt_download_id:
            return

        logger.error("YouTube download failed: %s", msg)
        self._show_error("YouTube download failed", msg)
        self.preview_status.configure(text="Error", text_color=C.COLOR_ERROR)

    def _load_youtube_url(self):
        """Explicitly download and load a YouTube URL."""
        url = self.youtube_entry.get().strip()
        if not url:
            self._show_error("No URL", "Please enter a YouTube URL.")
            return
        if not url.startswith("http"):
            self._show_error("Invalid URL", "URL must start with http:// or https://")
            return

        # If URL changed, delete the old cached file (keep only current track)
        for cached_url, cached_path in list(self._yt_cache.items()):
            if cached_url != url:
                if cached_path.exists():
                    try:
                        cached_path.unlink()
                    except Exception:
                        pass
                del self._yt_cache[cached_url]

        # Clear local state
        self._stop_preview()
        self.audio_path = None

        # Download (or reuse if same URL)
        self._download_and_play_youtube()

    def _parse_youtube_start_time(self, url: str):
        """
        Parse t= or start= parameter from a YouTube URL.

        Supports formats like 1h23m45s, 123s, or raw seconds.
        Sets the start time entry if found.
        """
        import re
        match = re.search(r'[?&](t|start)=([0-9hms]+)', url)
        if match:
            time_str = match.group(2)
            seconds = 0
            h_match = re.search(r'(\d+)h', time_str)
            m_match = re.search(r'(\d+)m', time_str)
            s_match = re.search(r'(\d+)s', time_str)
            if h_match:
                seconds += int(h_match.group(1)) * 3600
            if m_match:
                seconds += int(m_match.group(1)) * 60
            if s_match:
                seconds += int(s_match.group(1))
            if not h_match and not m_match and not s_match:
                seconds = int(time_str)

            self._yt_start = _format_time_hhmmss(seconds)
            self.start_time_entry.delete(0, "end")
            self.start_time_entry.insert(0, self._yt_start)

    def _start_ui_poll(self):
        """Start or restart the UI polling loop for playback progress updates."""
        if self._poll_id:
            self.after_cancel(self._poll_id)
        self._poll_ui()

    def _poll_ui(self):
        """
        UI refresh loop running ~10 fps during playback.

        Detects natural playback end and errors via thread-safe flags set by
        the audio callback thread. NEVER calls GUI code from the audio thread.
        """
        # Check for playback errors first
        if self._player._playback_error:
            err = self._player._playback_error
            self._player._playback_error = None
            self.preview_status.configure(text="Error", text_color=C.COLOR_ERROR)
            self.play_btn.configure(text="Play", command=self._play_preview)
            self._show_error("Playback failed", err)
            return

        if self._player.is_playing:
            self._draw_timeline()
            self._update_time_label()
            self._poll_id = self._safe_after(100, self._poll_ui)
        else:
            self._draw_timeline()
            self._update_time_label()
            self._poll_id = None

            # Check if playback finished naturally (audio thread set the flag)
            if self._player.finished_naturally:
                self._player.clear_finished_flag()
                self._handle_playback_finished()

    def _handle_playback_finished(self):
        """
        Handle end-of-playback on the main thread.

        Respects loop mode: if looping, restarts from segment start or track start.
        If not looping, resets to stopped state.
        """
        seg_end = getattr(self, '_segment_end', 0)

        if seg_end > 0:
            if self.loop_var.get():
                # Loop segment: restart from segment start
                start_sec = self._parse_time(self.start_time_entry.get())
                end_text = self.end_time_entry.get().strip()
                end_sec = self._parse_time(end_text) if end_text else self._player.duration
                self._segment_end = end_sec
                self._player.play(start_sec=start_sec, segment_end=end_sec)
                self._start_ui_poll()
                return
            else:
                # Segment finished, no loop: reset to start
                self._player._position = 0
                self._segment_end = 0
        else:
            if self.loop_var.get():
                # Loop full track: restart from 0
                self._player.play(start_sec=0)
                self._start_ui_poll()
                return
            else:
                # Track finished, no loop: reset to start
                self._player._position = 0

        self.preview_status.configure(text="Stopped", text_color=C.COLOR_TEXT_MUTED)
        self.play_btn.configure(text="Play", command=self._play_preview)
        self._update_time_label()
        self._draw_timeline()

    def _check_playback_init(self):
        """
        Check if audio playback failed immediately after starting.

        Common causes: no audio device, device in use, missing drivers.
        """
        if self._player._init_failed:
            self._player._init_failed = False  # Clear the flag
            self.preview_status.configure(text="Error", text_color=C.COLOR_ERROR)
            self.play_btn.configure(text="Play", command=self._play_preview)
            if self._poll_id:
                self.after_cancel(self._poll_id)
                self._poll_id = None
            self._show_error("Audio Playback Failed",
                             "Could not initialize audio playback.\n\n"
                             "Common causes:\n"
                             "• No audio output device connected\n"
                             "• Audio device in use by another application\n"
                             "• Missing audio drivers\n\n"
                             "You can still configure audio and build — "
                             "playback preview just won't work.")

    def _pause_preview(self):
        """Pause playback and update UI state."""
        self._player.pause()
        self.preview_status.configure(text="Paused", text_color=C.COLOR_WARNING)
        self.play_btn.configure(text="Play", command=self._resume_preview)
        if self._poll_id:
            self.after_cancel(self._poll_id)
            self._poll_id = None
        self._update_time_label()

    def _resume_preview(self):
        """Resume from pause and restart UI poll loop."""
        self._player.resume()
        self.preview_status.configure(text="Playing", text_color=C.COLOR_SUCCESS)
        self.play_btn.configure(text="Pause", command=self._pause_preview)
        self._start_ui_poll()

    def _stop_preview(self):
        """Stop playback completely. Keeps _audio_loaded True so timeline stays visible."""
        self._player.stop()
        # Keep _audio_loaded True so timeline stays visible
        if self._poll_id:
            self.after_cancel(self._poll_id)
            self._poll_id = None
        self.preview_status.configure(text="Stopped", text_color=C.COLOR_TEXT_MUTED)
        self.play_btn.configure(text="Play", command=self._play_preview)
        # Reset position to start but keep timeline
        self._player._position = 0
        self._update_time_label()
        self._draw_timeline()
