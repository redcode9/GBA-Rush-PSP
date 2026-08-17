# Copyright (C) 2026 JxP
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Credits Dialog Mixin for BuilderApp.

Renders CREDITS.md content in a scrollable modal dialog with clickable
hyperlinks, bold text, and bullet formatting. All content comes from the
file — no hardcoded text.
"""

import re
import webbrowser

import customtkinter as ctk
import tkinter as tk

from gui_utils import parse_credits_md
import constants as C


class CreditsMixin:
    """Mixin providing the Credits / About dialog."""

    def _get_credits_font(self, size=11):
        """
        Return a font tuple for the credits dialog, with cross-platform fallback.

        Tries system UI fonts in order of preference.
        """
        from tkinter import font as tkfont
        available = set(tkfont.families())
        candidates = [
            "Segoe UI",
            "Helvetica",
            "Arial",
            "DejaVu Sans",
        ]
        for family in candidates:
            if family in available:
                return (family, size)
        return ("TkDefaultFont", size)  # guaranteed to exist

    def _show_credits(self):
        """
        Show credits / about modal with clickable hyperlinks.

        Renders CREDITS.md content 1:1 — no hardcoded text.
        All content comes from the file, including title and subtitle.
        """
        dlg = ctk.CTkToplevel(self)
        dlg.title("About")
        dlg.configure(fg_color=C.COLOR_BG_MEDIUM)
        dlg.transient(self)
        dlg.grab_set()
        dlg.resizable(False, False)

        # Center on parent
        self.update_idletasks()
        px, py = self.winfo_x(), self.winfo_y()
        pw, ph = self.winfo_width(), self.winfo_height()
        dw = 600
        # Cap height to 80% of parent so it never overflows small screens
        max_dh = int(ph * 0.8)
        dh = min(650, max_dh)
        dlg.geometry(f"{dw}x{dh}+{px + (pw - dw)//2}+{py + (ph - dh)//2}")

        # Scrollable text frame
        text_frame = ctk.CTkFrame(dlg, fg_color=C.COLOR_BG_DARK, corner_radius=0)
        text_frame.pack(padx=20, pady=(20, 10), fill="both", expand=True)

        text = tk.Text(text_frame, width=70, height=30, bg=C.COLOR_BG_DARK, fg=C.COLOR_TEXT_MAIN,
                       font=self._get_credits_font(11), wrap="word", relief="flat",
                       borderwidth=0, highlightthickness=0,
                       padx=15, pady=15, cursor="arrow",
                       selectbackground=C.COLOR_ACCENT, selectforeground="#ffffff")
        text.pack(side="left", fill="both", expand=True)

        scrollbar = ctk.CTkScrollbar(text_frame, command=text.yview)
        scrollbar.pack(side="right", fill="y", padx=(0, 5), pady=5)
        text.configure(yscrollcommand=scrollbar.set)

        # Tag styles for rich text rendering
        family = self._get_credits_font()[0]

        text.tag_configure("h1", font=(family, 18, "bold"), foreground="#ffffff", spacing1=10, spacing3=10)
        text.tag_configure("h2", font=(family, 14, "bold"), foreground="#4fc3f7", spacing1=16, spacing3=8)
        text.tag_configure("body", font=(family, 11), foreground="#cccccc", spacing1=4, spacing3=4)
        text.tag_configure("bold", font=(family, 11, "bold"), foreground=C.COLOR_TEXT_MAIN)
        text.tag_configure("link", font=(family, 11), foreground="#4fc3f7", underline=True)
        text.tag_configure("muted", font=(family, 10), foreground="#888888")
        text.tag_configure("bullet", font=(family, 11), foreground=C.COLOR_TEXT_DIM)
        text.tag_configure("separator", font=(family, 1), foreground="#333333", spacing1=12, spacing3=12)

        # Link tracking: maps tag name -> URL
        links = {}
        link_counter = [0]

        def add_link(display: str, url: str):
            """
            Insert a clickable hyperlink into the text widget.

            Supports **bold** markup inside the link display text.
            """
            tag = f"url_{link_counter[0]}"
            link_counter[0] += 1
            links[tag] = url

            # Parse **bold** inside the link display text
            bold_pattern = r'\*\*([^\*]+)\*\*'
            bold_matches = list(re.finditer(bold_pattern, display))

            if not bold_matches:
                text.insert("end", display, ("link", tag))
            else:
                last = 0
                for bm in bold_matches:
                    if bm.start() > last:
                        text.insert("end", display[last:bm.start()], ("link", tag))
                    text.insert("end", bm.group(1), ("link", tag, "bold"))
                    last = bm.end()
                if last < len(display):
                    text.insert("end", display[last:], ("link", tag))

        def render_inline_markup(text_str: str, base_tag="body"):
            """
            Render text with **bold** and [links](url) markup.

            Processes text left-to-right, handling overlapping patterns.
            """
            # Combined pattern: match either **bold** or [link](url)
            pattern = r'(\*\*[^\*]+\*\*)|(\[([^\]]+)\]\(([^)]+)\))'

            last_end = 0
            for match in re.finditer(pattern, text_str):
                # Text before the match
                before = text_str[last_end:match.start()]
                if before:
                    text.insert("end", before, base_tag)

                if match.group(1):  # **bold** text
                    bold_text = match.group(1)[2:-2]  # Strip ** wrappers
                    text.insert("end", bold_text, "bold")
                elif match.group(2):  # [display](url) link
                    add_link(match.group(3), match.group(4))

                last_end = match.end()

            # Remaining text after last match
            remaining = text_str[last_end:]
            if remaining:
                text.insert("end", remaining, base_tag)

        def on_enter(e, t=text):
            t.config(cursor="hand2")

        def on_leave(e, t=text):
            t.config(cursor="arrow")

        # --- Parse and Render CREDITS.md ---
        credits_md = self._get_base_dir() / "CREDITS.md"
        data = parse_credits_md(credits_md)

        # Title (from CREDITS.md # header)
        if data.get("title"):
            text.insert("end", data["title"] + "\n", "h1")

        # Sections
        for section in data.get("sections", []):
            text.insert("end", section["name"] + "\n", "h2")

            for item in section.get("items", []):
                item_type = item.get("type")
                item_text = item.get("text", "")

                if item_type == "text":
                    # Plain paragraph — no bullet
                    render_inline_markup(item_text, "body")
                    text.insert("end", "\n", "body")

                elif item_type == "bullet":
                    # Check if it's a timeline item (starts with **YYYY**)
                    year_match = re.match(r'^(\*\*\d{4}(?:-\d{4})?\*\*)\s*[—\-]\s*(.*)$', item_text)
                    if year_match:
                        text.insert("end", "  ", "body")
                        text.insert("end", year_match.group(1).replace("**", ""), "bold")
                        text.insert("end", " — ", "body")
                        render_inline_markup(year_match.group(2), "body")
                    else:
                        # Regular bullet item
                        text.insert("end", "  • ", "bullet")
                        render_inline_markup(item_text, "body")
                    text.insert("end", "\n", "body")

            text.insert("end", "\n", "separator")

        # Footer
        for line in data.get("footer", []):
            render_inline_markup(line, "muted")
            text.insert("end", "\n", "muted")

        # Bind links to open browser on click
        for tag, url in links.items():
            text.tag_bind(tag, "<Button-1>", lambda e, u=url: webbrowser.open(u))
            text.tag_bind(tag, "<Enter>", on_enter)
            text.tag_bind(tag, "<Leave>", on_leave)

        # Disable editing
        text.configure(state="disabled")

        # Close button
        ctk.CTkButton(dlg, text="Close", width=100, command=dlg.destroy).pack(pady=(0, 20))