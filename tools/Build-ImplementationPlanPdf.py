#!/usr/bin/env python3
"""Markdown 정본에서 한글 단계별 구현 계획서 PDF를 생성한다."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    BaseDocTemplate,
    Frame,
    KeepTogether,
    PageBreak,
    PageTemplate,
    Paragraph,
    Preformatted,
    Spacer,
    Table,
    TableStyle,
)
from reportlab.platypus.tableofcontents import TableOfContents


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "doc" / "VOLUMETRIC_CLOUD_IMPLEMENTATION_PLAN.md"
OUTPUT = ROOT / "doc" / "Volumetric Cloud 프로젝트 단계별 구현 계획서.pdf"
FONT_REGULAR = Path(r"C:\WINDOWS\Fonts\malgun.ttf")
FONT_BOLD = Path(r"C:\WINDOWS\Fonts\malgunbd.ttf")


class PlanDocument(BaseDocTemplate):
    def __init__(self, filename: str, **kwargs):
        super().__init__(filename, **kwargs)
        frame = Frame(self.leftMargin, self.bottomMargin, self.width, self.height, id="body")
        self.addPageTemplates(PageTemplate(id="main", frames=frame, onPage=self.draw_page))

    def draw_page(self, canvas, doc):
        canvas.saveState()
        canvas.setFont("Malgun", 8)
        canvas.setFillColor(colors.HexColor("#65758B"))
        canvas.drawString(self.leftMargin, 12 * mm, "Volumetric Cloud · 단계별 구현 계획서")
        canvas.drawRightString(A4[0] - self.rightMargin, 12 * mm, str(doc.page))
        canvas.setStrokeColor(colors.HexColor("#D6DEE8"))
        canvas.line(self.leftMargin, 17 * mm, A4[0] - self.rightMargin, 17 * mm)
        canvas.restoreState()

    def afterFlowable(self, flowable):
        if not isinstance(flowable, Paragraph):
            return
        level = getattr(flowable, "outline_level", None)
        if level is None:
            return
        text = flowable.getPlainText()
        key = f"section-{self.seq.nextf('section')}"
        self.canv.bookmarkPage(key)
        self.canv.addOutlineEntry(text, key, level=level, closed=False)
        self.notify("TOCEntry", (level, text, self.page, key))


def register_fonts() -> None:
    if not FONT_REGULAR.exists() or not FONT_BOLD.exists():
        raise FileNotFoundError("맑은 고딕 폰트를 찾을 수 없습니다.")
    pdfmetrics.registerFont(TTFont("Malgun", str(FONT_REGULAR)))
    pdfmetrics.registerFont(TTFont("Malgun-Bold", str(FONT_BOLD)))
    pdfmetrics.registerFontFamily("Malgun", normal="Malgun", bold="Malgun-Bold")


def esc(text: str) -> str:
    text = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    text = re.sub(r"`([^`]+)`", r"<font name='Malgun-Bold'>\1</font>", text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", text)
    return text


def styles():
    base = getSampleStyleSheet()
    return {
        "title": ParagraphStyle("TitleK", parent=base["Title"], fontName="Malgun-Bold", fontSize=25,
            leading=34, textColor=colors.HexColor("#102A43"), alignment=TA_CENTER, spaceAfter=10 * mm),
        "h1": ParagraphStyle("H1K", fontName="Malgun-Bold", fontSize=17, leading=23,
            textColor=colors.HexColor("#0B7285"), spaceBefore=6 * mm, spaceAfter=3 * mm),
        "h2": ParagraphStyle("H2K", fontName="Malgun-Bold", fontSize=13, leading=18,
            textColor=colors.HexColor("#1F3A5F"), spaceBefore=5 * mm, spaceAfter=2.5 * mm),
        "h3": ParagraphStyle("H3K", fontName="Malgun-Bold", fontSize=10.5, leading=15,
            textColor=colors.HexColor("#334E68"), spaceBefore=3 * mm, spaceAfter=1.5 * mm),
        "body": ParagraphStyle("BodyK", fontName="Malgun", fontSize=9, leading=14.3,
            textColor=colors.HexColor("#243B53"), alignment=TA_LEFT, spaceAfter=2 * mm),
        "bullet": ParagraphStyle("BulletK", fontName="Malgun", fontSize=9, leading=14,
            leftIndent=5 * mm, firstLineIndent=-3 * mm, bulletIndent=1 * mm, spaceAfter=1.2 * mm),
        "quote": ParagraphStyle("QuoteK", fontName="Malgun", fontSize=9, leading=14,
            leftIndent=5 * mm, rightIndent=5 * mm, borderColor=colors.HexColor("#63B3ED"),
            borderWidth=1, borderPadding=5, backColor=colors.HexColor("#EBF8FF"), spaceAfter=3 * mm),
        "code": ParagraphStyle("CodeK", fontName="Malgun", fontSize=8, leading=12,
            leftIndent=3 * mm, rightIndent=3 * mm, borderPadding=6,
            backColor=colors.HexColor("#F4F7FA"), textColor=colors.HexColor("#102A43"), spaceAfter=3 * mm),
        "toc_h": ParagraphStyle("TOCH", fontName="Malgun-Bold", fontSize=18, leading=24,
            textColor=colors.HexColor("#0B7285"), spaceAfter=5 * mm),
    }


def parse_table(lines: list[str], style) -> Table:
    rows = []
    for line in lines:
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if all(re.fullmatch(r":?-{3,}:?", cell) for cell in cells):
            continue
        rows.append([Paragraph(esc(cell), style) for cell in cells])
    count = max(len(row) for row in rows)
    widths = [170 * mm / count] * count
    table = Table(rows, colWidths=widths, repeatRows=1, hAlign="LEFT")
    table.setStyle(TableStyle([
        ("FONTNAME", (0, 0), (-1, 0), "Malgun-Bold"),
        ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#D9EEF2")),
        ("TEXTCOLOR", (0, 0), (-1, 0), colors.HexColor("#16324F")),
        ("GRID", (0, 0), (-1, -1), 0.4, colors.HexColor("#B8C7D9")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 4),
        ("RIGHTPADDING", (0, 0), (-1, -1), 4),
        ("TOPPADDING", (0, 0), (-1, -1), 4),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#F7FAFC")]),
    ]))
    return table


def build_story(text: str, s: dict) -> list:
    lines = text.splitlines()
    story = []
    index = 0
    first_heading = True
    while index < len(lines):
        line = lines[index].rstrip()
        if not line:
            index += 1
            continue
        if line.startswith("```"):
            code = []
            index += 1
            while index < len(lines) and not lines[index].startswith("```"):
                code.append(lines[index])
                index += 1
            story.append(Preformatted("\n".join(code), s["code"])); index += 1; continue
        if line.startswith("|") and index + 1 < len(lines) and lines[index + 1].startswith("|"):
            table_lines = []
            while index < len(lines) and lines[index].startswith("|"):
                table_lines.append(lines[index]); index += 1
            story.append(KeepTogether([parse_table(table_lines, s["body"]), Spacer(1, 3 * mm)])); continue
        heading = re.match(r"^(#{1,3})\s+(.+)$", line)
        if heading:
            level = len(heading.group(1)) - 1
            title = heading.group(2)
            if first_heading:
                p = Paragraph(esc(title), s["title"])
                story.extend([Spacer(1, 45 * mm), p,
                    Paragraph("DirectX 11 + HLSL · 대규모 평면 구름층 개정판", s["quote"]),
                    PageBreak(), Paragraph("목차", s["toc_h"])])
                toc = TableOfContents()
                toc.levelStyles = [
                    ParagraphStyle("TOC1", fontName="Malgun-Bold", fontSize=10, leading=16,
                        leftIndent=0, firstLineIndent=0, textColor=colors.HexColor("#1F3A5F")),
                    ParagraphStyle("TOC2", fontName="Malgun", fontSize=8.8, leading=14,
                        leftIndent=7 * mm, firstLineIndent=0, textColor=colors.HexColor("#486581")),
                    ParagraphStyle("TOC3", fontName="Malgun", fontSize=8.2, leading=13,
                        leftIndent=14 * mm, firstLineIndent=0, textColor=colors.HexColor("#627D98")),
                ]
                story.extend([toc, PageBreak()]); first_heading = False
            else:
                p = Paragraph(esc(title), s[f"h{level + 1}"])
                # 문서 제목(#)은 표지에서 소비하므로 본문의 ##도 최상위 북마크가 된다.
                p.outline_level = max(0, level - 1)
                story.append(p)
            index += 1; continue
        if line.startswith("> "):
            story.append(Paragraph(esc(line[2:]), s["quote"])); index += 1; continue
        if line.startswith("- "):
            story.append(Paragraph(esc(line[2:]), s["bullet"], bulletText="•")); index += 1; continue
        paragraph = [line]
        index += 1
        while index < len(lines) and lines[index].strip() and not re.match(r"^(#{1,3})\s|^- |^> |^```|^\|", lines[index]):
            paragraph.append(lines[index].strip()); index += 1
        story.append(Paragraph(esc(" ".join(paragraph)), s["body"]))
    return story


def main() -> int:
    register_fonts()
    markdown = SOURCE.read_text(encoding="utf-8")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    doc = PlanDocument(str(OUTPUT), pagesize=A4, leftMargin=20 * mm, rightMargin=20 * mm,
        topMargin=16 * mm, bottomMargin=20 * mm, title="Volumetric Cloud 프로젝트 단계별 구현 계획서",
        author="VolumetricCloud Project")
    doc.multiBuild(build_story(markdown, styles()))
    print(OUTPUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
