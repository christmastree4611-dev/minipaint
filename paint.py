#!/usr/bin/env python3
"""
Mini Paint - a small Microsoft Paint clone.
Pure Python + tkinter. No external dependencies.
Runs on Windows / macOS / Linux.

Usage:
    python paint.py
"""

import tkinter as tk
from tkinter import filedialog, colorchooser, messagebox
from tkinter import ttk

try:
    from PIL import Image, ImageTk, ImageGrab
    HAS_PIL = True
except ImportError:
    HAS_PIL = False


class MiniPaint:
    # Canvas size
    CANVAS_W = 900
    CANVAS_H = 600

    # Default palette (Windows Paint style)
    PALETTE = [
        "#000000", "#7F7F7F", "#880015", "#ED1C24", "#FF7F27", "#FFF200",
        "#22B14C", "#00A2E8", "#3F48CC", "#A349A4", "#FFFFFF", "#C3C3C3",
        "#B97A57", "#FFAEC9", "#FFC90E", "#EFE4B0", "#B5E61D", "#99D9EA",
        "#7092BE", "#C8BFE7",
    ]

    def __init__(self, root):
        self.root = root
        self.root.title("Mini Paint")
        self.root.geometry(f"{self.CANVAS_W + 180}x{self.CANVAS_H + 80}")
        self.root.minsize(800, 500)

        # State
        self.tool = "pencil"
        self.color = "#000000"
        self.fill_color = ""        # empty = no fill
        self.size = 3
        self.start_x = self.start_y = None
        self.last_x = self.last_y = None
        self.snapshot = None        # for shape preview
        self.undo_stack = []

        self._build_ui()

    # ---------------- UI ----------------
    def _build_ui(self):
        # ---- Top toolbar ----
        top = tk.Frame(self.root, relief=tk.RIDGE, bd=1)
        top.pack(side=tk.TOP, fill=tk.X)

        self._add_tool_btn(top, "pencil", "✏️ Pencil", "Freehand drawing")
        self._add_tool_btn(top, "line", "📏 Line", "Straight line")
        self._add_tool_btn(top, "rect", "▭ Rect", "Rectangle outline")
        self._add_tool_btn(top, "ellipse", "◯ Ellipse", "Ellipse outline")
        self._add_tool_btn(top, "fill_rect", "▬ Fill Rect", "Filled rectangle")
        self._add_tool_btn(top, "fill_ellipse", "● Fill Ellipse", "Filled ellipse")
        self._add_tool_btn(top, "eraser", "🧽 Eraser", "Erase by drawing white")
        self._add_tool_btn(top, "bucket", "🪣 Fill", "Flood fill area")

        sep = ttk.Separator(top, orient=tk.VERTICAL)
        sep.pack(side=tk.LEFT, padx=6, fill=tk.Y)

        # Brush size
        tk.Label(top, text="Size:").pack(side=tk.LEFT, padx=(4, 0))
        self.size_var = tk.IntVar(value=self.size)
        size_slider = ttk.Scale(top, from_=1, to=40, variable=self.size_var,
                               orient=tk.HORIZONTAL, length=120,
                               command=self._on_size_change)
        size_slider.pack(side=tk.LEFT, padx=4)
        self.size_label = tk.Label(top, text=str(self.size), width=3)
        self.size_label.pack(side=tk.LEFT)

        sep2 = ttk.Separator(top, orient=tk.VERTICAL)
        sep2.pack(side=tk.LEFT, padx=6, fill=tk.Y)

        # Action buttons
        tk.Button(top, text="↩ Undo", command=self.undo).pack(side=tk.LEFT, padx=2)
        tk.Button(top, text="🗑 Clear", command=self.clear).pack(side=tk.LEFT, padx=2)
        tk.Button(top, text="💾 Save", command=self.save).pack(side=tk.LEFT, padx=2)
        tk.Button(top, text="📂 Open", command=self.open_file).pack(side=tk.LEFT, padx=2)

        # ---- Middle: palette + canvas ----
        body = tk.Frame(self.root)
        body.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        # Left palette panel
        side = tk.Frame(body, relief=tk.RIDGE, bd=1, width=160)
        side.pack(side=tk.LEFT, fill=tk.Y)
        side.pack_propagate(False)

        tk.Label(side, text="Colors", font=("Segoe UI", 10, "bold")).pack(pady=(6, 2))

        # Current color swatches (foreground + background/fill)
        swatch_row = tk.Frame(side)
        swatch_row.pack(pady=4)
        self.fg_btn = tk.Button(swatch_row, bg=self.color, width=4, height=2,
                                relief=tk.RAISED, command=self.pick_color)
        self.fg_btn.grid(row=0, column=0, padx=2)
        self.fill_var = tk.BooleanVar(value=False)
        fill_chk = tk.Checkbutton(side, text="Fill shape",
                                  variable=self.fill_var,
                                  command=self._toggle_fill)
        fill_chk.pack(anchor=tk.W, padx=8)

        # Palette grid
        pal = tk.Frame(side)
        pal.pack(padx=8, pady=6)
        for i, c in enumerate(self.PALETTE):
            b = tk.Button(pal, bg=c, width=3, height=1,
                          relief=tk.FLAT, bd=1,
                          command=lambda col=c: self.set_color(col))
            b.grid(row=i // 10, column=i % 10, padx=1, pady=1, sticky="nsew")

        # ---- Canvas ----
        canvas_frame = tk.Frame(body, relief=tk.SUNKEN, bd=2)
        canvas_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=4, pady=4)

        self.canvas = tk.Canvas(canvas_frame, width=self.CANVAS_W,
                                height=self.CANVAS_H, bg="white",
                                cursor="crosshair", highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)

        # Bind events
        self.canvas.bind("<Button-1>", self.on_press)
        self.canvas.bind("<B1-Motion>", self.on_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_release)
        # Smooth drawing for pencil/eraser
        self.canvas.bind("<Motion>", self.on_motion_smooth)

        # Status bar
        self.status = tk.StringVar(value="Ready — pick a tool and start drawing.")
        bar = tk.Label(self.root, textvariable=self.status,
                       relief=tk.SUNKEN, anchor=tk.W, bd=1)
        bar.pack(side=tk.BOTTOM, fill=tk.X)

        self._push_undo()

    def _add_tool_btn(self, parent, name, label, tip):
        btn = tk.Button(parent, text=label, relief=tk.RAISED,
                        command=lambda: self.set_tool(name))
        btn.pack(side=tk.LEFT, padx=2, pady=4)
        # store reference for active-state styling
        if not hasattr(self, "_tool_btns"):
            self._tool_btns = {}
        self._tool_btns[name] = btn
        if name == self.tool:
            btn.config(relief=tk.SUNKEN)

    # ---------------- Tool / color ----------------
    def set_tool(self, name):
        self.tool = name
        for n, b in getattr(self, "_tool_btns", {}).items():
            b.config(relief=tk.SUNKEN if n == name else tk.RAISED)
        self.status.set(f"Tool: {name}")

    def set_color(self, c):
        self.color = c
        self.fg_btn.config(bg=c)

    def pick_color(self):
        c = colorchooser.askcolor(title="Pick a color")
        if c and c[1]:
            self.set_color(c[1])

    def _on_size_change(self, _):
        self.size = int(self.size_var.get())
        self.size_label.config(text=str(self.size))

    def _toggle_fill(self):
        if self.fill_var.get():
            self.fill_color = self.color
        else:
            self.fill_color = ""

    # ---------------- Drawing ----------------
    def on_press(self, e):
        self.start_x, self.start_y = e.x, e.y
        self.last_x, self.last_y = e.x, e.y

        if self.tool in ("pencil", "eraser"):
            self._draw_segment(e.x, e.y, e.x, e.y)
        elif self.tool == "bucket":
            self._flood_fill(e.x, e.y)
            self._push_undo()

    def on_motion_smooth(self, e):
        # Use B1-Motion primarily; this is just a no-op hook for non-button moves
        pass

    def on_drag(self, e):
        if self.tool in ("pencil", "eraser"):
            self._draw_segment(self.last_x, self.last_y, e.x, e.y)
            self.last_x, self.last_y = e.x, e.y
        elif self.tool in ("line", "rect", "ellipse", "fill_rect", "fill_ellipse"):
            # Save snapshot on first move to enable preview
            if self.snapshot is None:
                self.snapshot = self._grab_canvas_image()
            self._draw_shape_preview(self.start_x, self.start_y, e.x, e.y)

    def on_release(self, e):
        if self.tool in ("line", "rect", "ellipse", "fill_rect", "fill_ellipse"):
            # Final commit
            self._restore_canvas_image(self.snapshot)
            self.snapshot = None
            self._draw_shape_final(self.start_x, self.start_y, e.x, e.y)
            self._push_undo()
        elif self.tool in ("pencil", "eraser"):
            self._push_undo()

    def _draw_segment(self, x1, y1, x2, y2):
        color = "white" if self.tool == "eraser" else self.color
        w = self.size if self.tool != "eraser" else self.size * 2
        self.canvas.create_line(x1, y1, x2, y2, fill=color, width=w,
                                capstyle=tk.ROUND, joinstyle=tk.ROUND,
                                smooth=True, splinesteps=12)

    def _draw_shape_preview(self, x1, y1, x2, y2):
        self._restore_canvas_image(self.snapshot)
        self._draw_shape_final(x1, y1, x2, y2, preview=True)

    def _draw_shape_final(self, x1, y1, x2, y2, preview=False):
        outline = self.color
        fill = self.fill_color if (self.fill_var.get()
                                    and self.tool in ("fill_rect", "fill_ellipse")) else ""
        if self.tool in ("fill_rect", "rect"):
            self.canvas.create_rectangle(x1, y1, x2, y2,
                                         outline=outline, width=self.size,
                                         fill=fill)
        elif self.tool in ("fill_ellipse", "ellipse"):
            self.canvas.create_oval(x1, y1, x2, y2,
                                    outline=outline, width=self.size,
                                    fill=fill)
        elif self.tool == "line":
            self.canvas.create_line(x1, y1, x2, y2, fill=outline,
                                    width=self.size,
                                    capstyle=tk.ROUND)

    # ---- Canvas snapshot for shape preview ----
    def _grab_canvas_image(self):
        # Store a list of all current canvas items so we can delete and redraw
        return list(self.canvas.find_all())

    def _restore_canvas_image(self, snapshot):
        if snapshot is None:
            return
        # Delete items created AFTER the snapshot boundary
        current = list(self.canvas.find_all())
        # Map ids to indices; anything beyond snapshot length gets removed
        snap_set = set(snapshot)
        for item in current:
            if item not in snap_set:
                self.canvas.delete(item)

    # ---- Flood fill ----
    def _flood_fill(self, x, y):
        """Flood fill on tkinter canvas items is approximate. We use a
        simple approach: convert the canvas to a PIL image, flood-fill
        there, then redraw. Falls back to a no-op if PIL is missing."""
        if not HAS_PIL:
            messagebox.showwarning("Fill",
                                   "Flood fill requires Pillow: pip install pillow")
            return
        # Grab current canvas as image
        self.root.update()
        x0 = self.canvas.winfo_rootx()
        y0 = self.canvas.winfo_rooty()
        w = self.canvas.winfo_width()
        h = self.canvas.winfo_height()
        img = ImageGrab.grab(bbox=(x0, y0, x0 + w, y0 + h)).convert("RGB")
        # Note: x, y are canvas-relative, which matches the grabbed bbox
        self._flood_fill_pil(img, x, y, self.color)
        # Clear canvas and redraw the filled image
        self.canvas.delete("all")
        self._bg_image = ImageTk.PhotoImage(img)
        self.canvas.create_image(0, 0, anchor=tk.NW, image=self._bg_image)

    @staticmethod
    def _flood_fill_pil(img, x, y, fill_hex):
        fill_rgb = tuple(int(fill_hex.lstrip("#")[i:i+2], 16) for i in (0, 2, 4))
        px = img.load()
        w, h = img.size
        if not (0 <= x < w and 0 <= y < h):
            return
        target = px[x, y]
        if target == fill_rgb:
            return
        stack = [(x, y)]
        while stack:
            cx, cy = stack.pop()
            if not (0 <= cx < w and 0 <= cy < h):
                continue
            if px[cx, cy] != target:
                continue
            px[cx, cy] = fill_rgb
            stack.extend([(cx+1, cy), (cx-1, cy), (cx, cy+1), (cx, cy-1)])

    # ---------------- Undo / Clear ----------------
    def _push_undo(self):
        # Snapshot current canvas item config dicts
        items = []
        for item in self.canvas.find_all():
            try:
                cfg = self.canvas.itemconfig(item)
                cfg = {k: v[4] if len(v) > 4 else None for k, v in cfg.items()}
                coords = self.canvas.coords(item)
                itype = self.canvas.type(item)
                items.append((item, itype, coords, cfg))
            except tk.TclError:
                continue
        self.undo_stack.append(items)
        # Cap undo history
        if len(self.undo_stack) > 30:
            self.undo_stack.pop(0)

    def undo(self):
        if len(self.undo_stack) <= 1:
            self.status.set("Nothing to undo.")
            return
        self.undo_stack.pop()  # drop current
        prev = self.undo_stack[-1]
        self.canvas.delete("all")
        # Re-create items. Note: image items cannot be perfectly restored,
        # so this works best for vector shapes & lines.
        for _old_id, itype, coords, cfg in prev:
            if itype == "line":
                new_id = self.canvas.create_line(*coords)
            elif itype == "oval":
                new_id = self.canvas.create_oval(*coords)
            elif itype == "rectangle":
                new_id = self.canvas.create_rectangle(*coords)
            elif itype == "image":
                # Skip image restoration (flood-filled state)
                continue
            else:
                continue
            for k, v in cfg.items():
                if v is None:
                    continue
                try:
                    self.canvas.itemconfig(new_id, **{k: v})
                except tk.TclError:
                    pass
        self.status.set("Undid last action.")

    def clear(self):
        if not messagebox.askyesno("Clear", "Clear the whole canvas?"):
            return
        self.canvas.delete("all")
        self.canvas.config(bg="white")
        self._push_undo()
        self.status.set("Canvas cleared.")

    # ---------------- Save / Open ----------------
    def save(self):
        if not HAS_PIL:
            messagebox.showwarning("Save",
                                    "Saving as PNG requires Pillow: pip install pillow")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".png",
            filetypes=[("PNG", "*.png"), ("JPEG", "*.jpg"), ("BMP", "*.bmp")],
            title="Save drawing as")
        if not path:
            return
        self.root.update()
        x0 = self.canvas.winfo_rootx()
        y0 = self.canvas.winfo_rooty()
        w = self.canvas.winfo_width()
        h = self.canvas.winfo_height()
        img = ImageGrab.grab(bbox=(x0, y0, x0 + w, y0 + h))
        img.save(path)
        self.status.set(f"Saved to {path}")

    def open_file(self):
        if not HAS_PIL:
            messagebox.showwarning("Open",
                                   "Opening images requires Pillow: pip install pillow")
            return
        path = filedialog.askopenfilename(
            filetypes=[("Image files", "*.png *.jpg *.jpeg *.bmp *.gif")],
            title="Open image")
        if not path:
            return
        img = Image.open(path).convert("RGB")
        # Resize to canvas if too big (keep aspect)
        cw, ch = self.canvas.winfo_width(), self.canvas.winfo_height()
        img.thumbnail((cw, ch))
        self.canvas.delete("all")
        self._bg_image = ImageTk.PhotoImage(img)
        self.canvas.create_image(0, 0, anchor=tk.NW, image=self._bg_image)
        self._push_undo()
        self.status.set(f"Opened {path}")


def main():
    root = tk.Tk()
    try:
        # Try to set a nicer default font on Windows
        root.option_add("*Font", "Segoe UI 9")
    except Exception:
        pass
    app = MiniPaint(root)
    root.mainloop()


if __name__ == "__main__":
    main()
