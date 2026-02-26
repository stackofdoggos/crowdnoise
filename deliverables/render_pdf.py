#!/usr/bin/env python3
"""
Render SUMMARY.md to two 1-page PDFs (minimal + designed) with no external deps.

This is intentionally tiny + deterministic (Type1 base fonts only).
"""

from __future__ import annotations

import json
import os
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional, Tuple


# ---------------------------
# Text sanitization / wrapping
# ---------------------------


_CHARMAP = {
    "→": "->",
    "≈": "~",
    "–": "-",
    "—": "--",
    "‑": "-",
    "“": '"',
    "”": '"',
    "’": "'",
    "‘": "'",
    "•": "-",
    "\u00a0": " ",
}


def sanitize_text(s: str) -> str:
    s = s.strip()
    for k, v in _CHARMAP.items():
        s = s.replace(k, v)
    # strip markdown emphasis markers
    s = s.replace("**", "").replace("*", "")
    return s


def pdf_escape(s: str) -> str:
    return (
        s.replace("\\", "\\\\")
        .replace("(", "\\(")
        .replace(")", "\\)")
        .replace("\r", "")
        .replace("\n", "")
    )


def approx_text_width(text: str, font_size: float) -> float:
    """
    Approx width in points for Helvetica-ish fonts.
    Not perfect—good enough for stable wrapping.
    """
    if not text:
        return 0.0

    # crude per-char weights (relative to font size)
    skinny = set("ilI.,:;!'|")
    wide = set("mwMW@%&")

    w = 0.0
    for ch in text:
        if ch == " ":
            w += 0.26
        elif ch in skinny:
            w += 0.28
        elif ch in wide:
            w += 0.85
        else:
            w += 0.52
    return w * font_size


def wrap_words(text: str, max_width_pt: float, font_size: float) -> List[str]:
    text = sanitize_text(text)
    if not text:
        return []
    words = text.split()
    lines: List[str] = []
    cur: List[str] = []

    def cur_width(candidate_words: List[str]) -> float:
        return approx_text_width(" ".join(candidate_words), font_size)

    for w in words:
        if not cur:
            cur = [w]
            continue
        if cur_width(cur + [w]) <= max_width_pt:
            cur.append(w)
        else:
            lines.append(" ".join(cur))
            cur = [w]

    if cur:
        lines.append(" ".join(cur))
    return lines


# ---------------------------
# SUMMARY.md parsing (simple)
# ---------------------------


@dataclass
class Section:
    title: str
    blocks: List[Tuple[str, str]]
    # blocks: ("p"|"bullet"|"num"|"ui_item", text)


@dataclass
class Doc:
    title: str
    tagline: str
    sections: List[Section]


def parse_summary_md(md: str) -> Doc:
    lines = [ln.rstrip("\n") for ln in md.splitlines()]

    title = "crowd·noise"
    tagline = ""
    sections: List[Section] = []
    cur: Optional[Section] = None

    in_table = False
    table_rows: List[Tuple[str, str, str]] = []

    def flush_table_into_cur():
        nonlocal in_table, table_rows, cur
        if not cur or not table_rows:
            in_table = False
            table_rows = []
            return
        for screen, vibe, what in table_rows:
            # IMPORTANT: keep this ASCII-only (PDF stream is latin-1 encoded).
            txt = sanitize_text(f"{screen} - {what} ({vibe})")
            cur.blocks.append(("ui_item", txt))
        in_table = False
        table_rows = []

    for raw in lines:
        s = raw.strip()
        if not s:
            flush_table_into_cur()
            continue
        if s.startswith("<!--"):
            continue
        if s.startswith("<div") or s.startswith("</div"):
            continue
        if s == "---":
            flush_table_into_cur()
            continue

        if s.startswith("# "):
            title = sanitize_text(s[2:].strip())
            continue
        if (not tagline) and s.startswith("*") and s.endswith("*") and len(s) > 2:
            tagline = sanitize_text(s.strip("*").strip())
            continue

        if s.startswith("## "):
            flush_table_into_cur()
            cur = Section(title=s[3:].strip(), blocks=[])
            sections.append(cur)
            continue

        if cur is None:
            continue

        if s.startswith("|") and s.endswith("|"):
            # table row
            parts = [p.strip() for p in s.strip("|").split("|")]
            if len(parts) >= 3 and parts[0].lower() != "screen" and not set(parts[0]) <= {"-"}:
                in_table = True
                screen, vibe, what = parts[0], parts[1], parts[2]
                table_rows.append((sanitize_text(screen), sanitize_text(vibe), sanitize_text(what)))
            continue

        m_num = re.match(r"^(\d+)\.\s+(.*)$", s)
        if m_num:
            cur.blocks.append(("num", f"{m_num.group(1)}. {sanitize_text(m_num.group(2))}"))
            continue
        if s.startswith("- "):
            cur.blocks.append(("bullet", sanitize_text(s[2:])))
            continue

        cur.blocks.append(("p", sanitize_text(s)))

    flush_table_into_cur()

    return Doc(title=title, tagline=tagline, sections=sections)


# ---------------------------
# PDF building (Type1 base fonts)
# ---------------------------


class PDF:
    def __init__(self):
        self.objects: List[bytes] = []

    def add_obj(self, data: bytes) -> int:
        self.objects.append(data)
        return len(self.objects)

    def build(self) -> bytes:
        # reserved: catalog, pages, page, fonts, contents are added by caller
        out = bytearray()
        out += b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n"
        offsets = [0]
        for i, obj in enumerate(self.objects, start=1):
            offsets.append(len(out))
            out += f"{i} 0 obj\n".encode("ascii")
            out += obj
            out += b"\nendobj\n"

        xref_start = len(out)
        out += f"xref\n0 {len(self.objects) + 1}\n".encode("ascii")
        out += b"0000000000 65535 f \n"
        for i in range(1, len(self.objects) + 1):
            out += f"{offsets[i]:010d} 00000 n \n".encode("ascii")

        out += b"trailer\n"
        out += f"<< /Size {len(self.objects) + 1} /Root 1 0 R >>\n".encode("ascii")
        out += b"startxref\n"
        out += f"{xref_start}\n".encode("ascii")
        out += b"%%EOF\n"
        return bytes(out)


def rgb(cmd: str, r: float, g: float, b: float) -> str:
    return f"{r:.3f} {g:.3f} {b:.3f} {cmd}\n"


def line_cmd(x1: float, y1: float, x2: float, y2: float) -> str:
    return f"{x1:.2f} {y1:.2f} m {x2:.2f} {y2:.2f} l S\n"


def rect_fill(x: float, y: float, w: float, h: float) -> str:
    return f"{x:.2f} {y:.2f} {w:.2f} {h:.2f} re f\n"


def text_cmd(font: str, size: float, x: float, y: float, text: str) -> str:
    # absolute positioning via Tm
    esc = pdf_escape(text)
    return f"BT /{font} {size:.2f} Tf 1 0 0 1 {x:.2f} {y:.2f} Tm ({esc}) Tj ET\n"


# ---------------------------
# Layout
# ---------------------------


@dataclass
class Theme:
    name: str
    page_w: float
    page_h: float
    margin: float
    colors: dict
    type_: dict
    layout: dict

    @staticmethod
    def load(path: Path) -> "Theme":
        data = json.loads(path.read_text(encoding="utf-8"))
        return Theme(
            name=data["name"],
            page_w=data["page"]["width"],
            page_h=data["page"]["height"],
            margin=data["page"]["margin"],
            colors=data["colors"],
            type_=data["type"],
            layout=data["layout"],
        )


def layout_pdf(doc: Doc, theme: Theme) -> Tuple[str, float]:
    """
    Returns (content_stream, bottom_y).
    If bottom_y < margin -> overflow.
    """
    W, H = theme.page_w, theme.page_h
    m = theme.margin
    colors = theme.colors
    t = theme.type_
    l = theme.layout

    # fonts: F1 Helvetica, F2 Helvetica-Bold, F3 Courier
    content = []

    # background (theme-driven)
    bg = colors.get("background")
    if bg:
        content.append(rgb("rg", *bg))
        content.append(rect_fill(0, 0, W, H))

    # background / panel for designed theme
    content_x = m
    content_w = W - 2 * m
    if l.get("two_column_ui"):
        panel_w = float(l.get("left_panel_width", 0))
        # panel fill
        pf = colors.get("panel_fill", [0.95, 0.95, 0.95])
        content.append(rgb("rg", *pf))
        content.append(rect_fill(0, 0, panel_w, H))
        content_x = panel_w + m
        content_w = W - content_x - m

        # panel text
        content.append(rgb("rg", *colors["text"]))
        py = H - 56
        content.append(text_cmd("F2", 11, 14, py, doc.title))
        py -= 18
        tags = ["collab", "sampling", "provenance", "album unlock"]
        for tag in tags:
            content.append(text_cmd("F1", 9, 14, py, tag))
            py -= 14

    # header
    y = H - m
    content.append(rgb("rg", *colors["text"]))

    title_size = float(t["title_size"])
    tagline_size = float(t["tagline_size"])

    title = doc.title
    title_align = l.get("title_align", "left")
    if title_align == "center":
        tx = (W - approx_text_width(title, title_size)) / 2
    else:
        tx = content_x

    y -= title_size
    content.append(text_cmd("F2", title_size, tx, y, title))
    y -= 10

    if doc.tagline:
        tag = doc.tagline
        if title_align == "center":
            ttx = (W - approx_text_width(tag, tagline_size)) / 2
        else:
            ttx = content_x
        content.append(rgb("rg", *colors["muted"]))
        y -= tagline_size
        content.append(text_cmd("F1", tagline_size, ttx, y, tag))
        content.append(rgb("rg", *colors["text"]))
        y -= 10

    # rule
    content.append(rgb("RG", *colors["rule"]))
    content.append("0.8 w\n")
    content.append(line_cmd(content_x, y, content_x + content_w, y))
    content.append(rgb("rg", *colors["text"]))
    y -= float(l.get("rule_gap", 10))

    body_size = float(t["body_size"])
    h2_size = float(t["h2_size"])
    leading = float(t["leading"])
    section_gap = float(l.get("section_gap", 10))

    def draw_paragraph(text: str, indent: float = 0.0):
        nonlocal y
        maxw = content_w - indent
        for ln in wrap_words(text, maxw, body_size):
            y -= leading
            content.append(text_cmd("F1", body_size, content_x + indent, y, ln))

    def draw_bullet(text: str):
        nonlocal y
        bullet = "- "
        indent = 12
        maxw = content_w - indent
        wrapped = wrap_words(text, maxw, body_size)
        if not wrapped:
            return
        y -= leading
        content.append(text_cmd("F1", body_size, content_x, y, bullet + wrapped[0]))
        for cont in wrapped[1:]:
            y -= leading
            content.append(text_cmd("F1", body_size, content_x + indent, y, cont))

    def draw_ui_grid(items: List[str]):
        nonlocal y
        # 2 columns, label in bold then rest normal, with robust row height calc
        col_gap = 18
        col_w = (content_w - col_gap) / 2
        left_x = content_x
        right_x = content_x + col_w + col_gap

        def split_label_rest(s: str) -> Tuple[str, str]:
            # we use ASCII hyphen separator from parse step
            if " - " in s:
                label, rest = s.split(" - ", 1)
                return (label.strip(), rest.strip())
            return ("", s)

        rows = [items[i : i + 2] for i in range(0, len(items), 2)]
        for row in rows:
            cells: List[Tuple[str, List[str]]] = []
            max_lines = 1

            for txt in row:
                label, rest = split_label_rest(txt)
                prefix = f"{label} - " if label else ""
                prefix_w = approx_text_width(prefix, body_size)
                maxw = max(20.0, col_w - prefix_w)
                rest_lines = wrap_words(rest, maxw, body_size) or [rest]
                cells.append((prefix, rest_lines))
                max_lines = max(max_lines, len(rest_lines))

            # draw the whole row from the same top baseline
            for line_idx in range(max_lines):
                y -= leading
                for col_idx, (prefix, rest_lines) in enumerate(cells):
                    x0 = left_x if col_idx == 0 else right_x
                    if line_idx == 0:
                        # prefix (bold) + first rest line
                        content.append(text_cmd("F2", body_size, x0, y, prefix))
                        px = x0 + approx_text_width(prefix, body_size)
                        rest = rest_lines[0] if rest_lines else ""
                        content.append(text_cmd("F1", body_size, px, y, rest))
                    else:
                        # subsequent wrap lines
                        if line_idx < len(rest_lines):
                            content.append(text_cmd("F1", body_size, x0 + 12, y, rest_lines[line_idx]))

            y -= 2

    for sec in doc.sections:
        # section title
        y -= section_gap
        content.append(rgb("rg", *colors["muted"]))
        y -= h2_size + 1
        content.append(text_cmd("F2", h2_size, content_x, y, sanitize_text(sec.title)))
        content.append(rgb("rg", *colors["text"]))

        ui_items = [txt for kind, txt in sec.blocks if kind == "ui_item"]
        if ui_items and l.get("two_column_ui"):
            draw_ui_grid(ui_items)
            continue

        for kind, txt in sec.blocks:
            if kind == "p":
                draw_paragraph(txt)
                y -= 2
            elif kind == "bullet":
                draw_bullet(txt)
            elif kind == "num":
                draw_paragraph(txt, indent=0.0)
            elif kind == "ui_item":
                draw_bullet(txt)

    return "".join(content), y


def scale_theme(theme: Theme, scale: float) -> Theme:
    data = {
        "name": theme.name,
        "page": {"width": theme.page_w, "height": theme.page_h, "margin": theme.margin},
        "colors": theme.colors,
        "type": dict(theme.type_),
        "layout": dict(theme.layout),
    }
    for k in ["title_size", "tagline_size", "h2_size", "body_size", "mono_size", "leading"]:
        if k in data["type"]:
            data["type"][k] = float(data["type"][k]) * scale
    for k in ["section_gap", "rule_gap"]:
        if k in data["layout"]:
            data["layout"][k] = float(data["layout"][k]) * scale
    tmp = Theme(
        name=data["name"],
        page_w=data["page"]["width"],
        page_h=data["page"]["height"],
        margin=data["page"]["margin"],
        colors=data["colors"],
        type_=data["type"],
        layout=data["layout"],
    )
    return tmp


def render_one(doc: Doc, theme_path: Path, out_path: Path) -> None:
    base_theme = Theme.load(theme_path)

    # iterative fit: shrink slightly until it fits on 1 page
    theme = base_theme
    for _ in range(6):
        stream, bottom_y = layout_pdf(doc, theme)
        if bottom_y >= theme.margin:
            break
        # overflow: shrink
        theme = scale_theme(theme, 0.94)

    stream_bytes = stream.encode("latin-1", "replace")

    pdf = PDF()

    # 1: catalog, 2: pages, 3: page, 4-6: fonts, 7: contents
    catalog_obj = pdf.add_obj(b"<< /Type /Catalog /Pages 2 0 R >>")
    assert catalog_obj == 1

    pages_obj = pdf.add_obj(b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    assert pages_obj == 2

    W, H = theme.page_w, theme.page_h

    page_obj = pdf.add_obj(
        f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {W:.0f} {H:.0f}] "
        f"/Resources << /Font << /F1 4 0 R /F2 5 0 R /F3 6 0 R >> >> "
        f"/Contents 7 0 R >>".encode("ascii")
    )
    assert page_obj == 3

    pdf.add_obj(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")  # F1
    pdf.add_obj(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>")  # F2
    pdf.add_obj(b"<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>")  # F3

    content_obj = (
        b"<< /Length " + str(len(stream_bytes)).encode("ascii") + b" >>\nstream\n" + stream_bytes + b"endstream"
    )
    pdf.add_obj(content_obj)

    out_path.write_bytes(pdf.build())


def main() -> int:
    root = Path(__file__).resolve().parent
    summary_path = root / "SUMMARY.md"
    pdf_dir = root / "deliverables" / "pdf"
    pdf_dir.mkdir(parents=True, exist_ok=True)

    doc = parse_summary_md(summary_path.read_text(encoding="utf-8"))

    render_one(doc, root / "theme_minimal.json", root / "crowdnoise_summary_minimal.pdf")
    render_one(doc, root / "theme_designed.json", pdf_dir / "crowdnoise_summary_designed.pdf")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

