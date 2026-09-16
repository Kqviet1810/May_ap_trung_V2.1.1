#!/usr/bin/env python3
"""Tao hai ban manual A5 v3.8.0 tu PDF da dan trang v3.7.1."""

from io import BytesIO
from pathlib import Path

from pypdf import PdfReader, PdfWriter
from reportlab.lib import colors
from reportlab.lib.pagesizes import A5
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "May_ap_trung_Huong_dan_van_hanh.pdf"
OUTPUTS = (BASE, ROOT / "audit/manual/OPERATION_USER_MANUAL_A5.pdf")
GREEN, INK = colors.HexColor("#0F6E56"), colors.HexColor("#16231F")
MUTED, LINE, WASH = (colors.HexColor("#5B6B64"), colors.HexColor("#D9E2DD"),
                     colors.HexColor("#F3F6F4"))


def p(text, style):
    return Paragraph(text, style)


def document(story):
    buf = BytesIO()
    doc = SimpleDocTemplate(buf, pagesize=A5, leftMargin=16 * mm,
                            rightMargin=14 * mm, topMargin=16 * mm,
                            bottomMargin=16 * mm)
    doc.build(story)
    return PdfReader(BytesIO(buf.getvalue())).pages[0]


def styles():
    base = getSampleStyleSheet()
    return {
        "h": ParagraphStyle("h", parent=base["Heading1"], fontName="MayapSans-Bold",
                            fontSize=13, leading=16, textColor=GREEN, spaceAfter=4 * mm),
        "sub": ParagraphStyle("sub", parent=base["Heading2"], fontName="MayapSans-Bold",
                              fontSize=8.5, leading=11, textColor=INK,
                              spaceBefore=3 * mm, spaceAfter=1.5 * mm),
        "body": ParagraphStyle("body", parent=base["Normal"], fontName="MayapSans",
                               fontSize=7.1, leading=10.5, textColor=INK),
        "small": ParagraphStyle("small", parent=base["Normal"], fontName="MayapSans",
                                fontSize=5.7, leading=8, textColor=MUTED),
        "cell": ParagraphStyle("cell", parent=base["Normal"], fontName="MayapSans",
                               fontSize=5.7, leading=8, textColor=INK),
        "head": ParagraphStyle("head", parent=base["Normal"], fontName="MayapSans-Bold",
                               fontSize=5.5, leading=7, textColor=MUTED),
    }


def cover_page():
    s = styles()
    brand = ParagraphStyle("brand", parent=s["body"], fontName="MayapSans-Bold",
                           fontSize=8.5, textColor=GREEN)
    title = ParagraphStyle("title", parent=s["h"], fontSize=21, leading=25,
                           textColor=INK, spaceAfter=0)
    label = ParagraphStyle("label", parent=s["head"], fontSize=6.2)
    value = ParagraphStyle("value", parent=s["body"], fontSize=7.2, leading=9)
    rows = [
        ("MODEL", "MAYAP INDUSTRIAL v3.8.0"),
        ("FIRMWARE", "v3.8.0 (điều khiển) / v3.8.0 (HMI)"),
        ("HARDWARE", "CTRL-S3-N8-R1 (ESP32-S3)"),
        ("DOCUMENT NO.", "MAYAP-OPS-A5-001"), ("REVISION", "Rev. 1.3"),
        ("DATE", "15/09/2026"),
    ]
    table = Table([[p(a, label), p(b, value)] for a, b in rows],
                  colWidths=[30 * mm, 78 * mm])
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (0, -1), WASH),
        ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
        ("LEFTPADDING", (0, 0), (-1, -1), 3),
        ("TOPPADDING", (0, 0), (-1, -1), 3),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
        ("LINEABOVE", (0, 0), (-1, 0), 0.6, LINE),
    ]))
    return document([
        p("MAYAP INDUSTRIAL", brand), Spacer(1, 49 * mm),
        p("HƯỚNG DẪN SỬ DỤNG<br/>&amp; KHẮC PHỤC SỰ CỐ", title),
        Spacer(1, 4 * mm), p("OPERATION &amp; TROUBLESHOOTING MANUAL", s["small"]),
        Spacer(1, 17 * mm), table, Spacer(1, 4 * mm),
        p("Tài liệu mô tả hành vi theo source code tại thời điểm phát hành. "
          "Các chức năng an toàn và vận hành phải được xác nhận trên phần "
          "cứng thật trước khi bàn giao chính thức.", s["small"]),
    ])


def toc_page():
    s = styles()
    item = ParagraphStyle("item", parent=s["body"], fontName="MayapSans-Bold",
                          fontSize=6.5, leading=8.5)
    entries = [
        ("1. Giới thiệu", 3), ("2. An toàn", 4), ("3. Tổng quan máy", 5),
        ("4. HMI (màn hình điều khiển tại máy)", 6),
        ("5. Web / Ứng dụng điều khiển từ xa", 8), ("6. Khởi động máy", 11),
        ("7. Chế độ AUTO", 12), ("8. Chế độ MANUAL", 13),
        ("9. Chuyển đổi AUTO ↔ MANUAL", 14), ("10. Quản lý mẻ ấp", 15),
        ("11. Hệ thống tự phục hồi", 17),
        ("12. Bảng tra cứu mã lỗi (Fault Card)", 18),
        ("13. Cây quyết định xử lý sự cố", 26),
        ("14. Mất điện / Khởi động lại bất thường", 28),
        ("15. Checklist vận hành", 29),
        ("16. Được phép / Không được phép", 30),
        ("17. Khi nào gọi kỹ thuật", 31),
        ("18. Phần dành cho kỹ thuật viên", 32),
        ("19. Giới hạn đã biết", 34),
        ("20. Bảng tổng hợp Recovery Matrix", 35),
        ("21. Lịch sử phát hành", 37),
    ]
    table = Table([[p(a, item), p(str(b), item)] for a, b in entries],
                  colWidths=[113 * mm, 10 * mm])
    table.setStyle(TableStyle([
        ("ALIGN", (1, 0), (1, -1), "RIGHT"),
        ("LINEBELOW", (0, 0), (-1, -1), 0.35, LINE),
        ("LEFTPADDING", (0, 0), (-1, -1), 0),
        ("RIGHTPADDING", (0, 0), (-1, -1), 0),
        ("TOPPADDING", (0, 0), (-1, -1), 2.5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 2.5),
    ]))
    return document([p("MỤC LỤC", s["h"]), table, Spacer(1, 3 * mm),
                     p("Số trang đã được đồng bộ theo bản PDF Rev. 1.3.", s["small"])])


def intro_page():
    s = styles()
    symbols = [
        ("BÌNH THƯỜNG", "Máy vận hành bình thường, không cần thao tác."),
        ("CẢNH BÁO", "Cần chú ý/theo dõi, máy có thể vẫn tiếp tục vận hành."),
        ("LỖI (STOP)", "Máy tự khóa chức năng cần thiết để bảo vệ an toàn."),
        ("KHẨN CẤP", "Hệ thống cắt nhiệt ngay lập tức."),
        ("KỸ THUẬT VIÊN", "Chỉ người có chuyên môn được xử lý."),
    ]
    table = Table([[p(a, s["sub"]), p(b, s["body"])] for a, b in symbols],
                  colWidths=[31 * mm, 92 * mm])
    table.setStyle(TableStyle([
        ("GRID", (0, 0), (-1, -1), 0.45, LINE),
        ("BACKGROUND", (0, 0), (0, -1), WASH),
        ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
        ("LEFTPADDING", (0, 0), (-1, -1), 4),
        ("TOPPADDING", (0, 0), (-1, -1), 3),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
    ]))
    return document([
        p("1. GIỚI THIỆU", s["h"]), p("Mục đích", s["sub"]),
        p("Tài liệu hướng dẫn vận hành, theo dõi và xử lý sự cố cơ bản cho máy "
          "ấp trứng công nghiệp MAYAP INDUSTRIAL, dựa trên firmware điều khiển "
          "và HMI v3.8.0.", s["body"]),
        p("Đối tượng sử dụng", s["sub"]),
        p("• Người vận hành hằng ngày.<br/>• Kỹ thuật viên hiện trường và người "
          "thực hiện provisioning MQTT/Cloud.", s["body"]),
        p("Phạm vi", s["sub"]),
        p("Vận hành AUTO/MANUAL, HMI và Web, quản lý mẻ ấp, mã lỗi, tự phục "
          "hồi, thông báo Push và cập nhật OTA. Quy trình tạo tài khoản HiveMQ, "
          "provision NVS/D1 và phát hành firmware nằm ở Phần 18 và README.", s["body"]),
        p("Ký hiệu dùng trong tài liệu", s["sub"]), table, Spacer(1, 3 * mm),
        p("Ghi chú nguồn: Nội dung kỹ thuật được đối chiếu từ source firmware, "
          "web, Worker, tài liệu audit và kết quả CI.", s["small"]),
    ])


def revision_page():
    s = styles()
    rows = [
        ("1.0", "27/08/2026", "Phát hành lần đầu từ phân tích firmware điều khiển/HMI.", "Claude Code"),
        ("1.1", "15/09/2026", "Đồng bộ v3.7.1; bổ sung HMI/Web nâng cao, Fault Card, health monitor và hướng dẫn vận hành.", "Claude Code"),
        ("1.2", "15/09/2026", "Đồng bộ v3.8.0; hoàn thiện E137, PIN riêng từng máy, TLS và hardening trước phát hành.", "OpenAI Codex"),
        ("1.3", "15/09/2026", "Credential MQTT/Cloud chuyển sang NVS giữ qua OTA; thêm CA ISRG/GTS, MQTT.js nội bộ và GitHub Release secret-free có SHA-256.", "OpenAI Codex"),
    ]
    data = [[p(x, s["head"]) for x in ("REV.", "NGÀY", "NỘI DUNG", "TÁC GIẢ")]]
    data += [[p(x, s["cell"]) for x in row] for row in rows]
    table = Table(data, colWidths=[10 * mm, 20 * mm, 75 * mm, 19 * mm])
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), WASH),
        ("GRID", (0, 0), (-1, -1), 0.45, LINE),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 3),
        ("TOPPADDING", (0, 0), (-1, -1), 4),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
    ]))
    return document([p("21. LỊCH SỬ PHÁT HÀNH", s["h"]), table, Spacer(1, 5 * mm),
                     p("Lưu ý trước bàn giao: CI xác nhận source và build nhưng "
                       "không thay thế thử nghiệm interlock, cảm biến, đảo trứng, "
                       "mất điện, MQTT, Push và OTA trên máy thật.", s["body"])])


def main():
    pdfmetrics.registerFont(TTFont("MayapSans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"))
    pdfmetrics.registerFont(TTFont("MayapSans-Bold", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"))
    base = PdfReader(BytesIO(BASE.read_bytes()))
    if len(base.pages) != 37:
        raise RuntimeError(f"Can PDF nen 37 trang, nhan {len(base.pages)}")
    writer = PdfWriter()
    for page in (cover_page(), toc_page(), intro_page()):
        writer.add_page(page)
    for page in base.pages[3:-1]:
        writer.add_page(page)
    writer.add_page(revision_page())
    writer.add_metadata({"/Title": "MAYAP INDUSTRIAL - Huong dan su dung va khac phuc su co",
                         "/Author": "MAYAP", "/Subject": "Firmware v3.8.0 - Revision 1.3"})
    payload = BytesIO()
    writer.write(payload)
    for output in OUTPUTS:
        output.write_bytes(payload.getvalue())
        print(output.relative_to(ROOT))


if __name__ == "__main__":
    main()
