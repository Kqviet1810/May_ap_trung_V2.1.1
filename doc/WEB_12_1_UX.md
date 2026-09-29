# MAYAP Web 12.1 — giao diện và kết nối

Tiếp nối `feat/adaptive-staged-boot`, firmware/HMI vẫn là 4.0.0. Thay đổi
này tập trung vào web; các file firmware ESP32, ATtiny, Adaptive Boot và
Runtime Recovery giữ nguyên so với `a60fe7e`.

## Cài đặt và ngôn ngữ

Các nhóm là giao diện/kết nối, cảnh báo/nhắc nhở, vận hành mẻ, quạt hút,
nhiệt độ/cảm biến, nâng cao và thiết bị/hỗ trợ. Nhãn nhóm nằm cùng section
với các thẻ của nhóm, tránh đặt cảm biến dưới tiêu đề quạt hút khi một thẻ
bị ẩn. Nhóm quạt luôn có thẻ riêng.

Quạt hút có ngưỡng bật/tắt theo nhiệt; thông gió tự động; mức Thấp/Tiêu
chuẩn/Cao; chu kỳ 40–120 phút, bước 10; tỷ lệ thời gian chạy 5–90% cho
ngày 1–3, 4–7, 8–11, 12–15, 16–18 và 19–21. Các giá trị mặc định và giới
hạn lấy từ cài đặt HMI hiện tại. Web kiểm tra nhiệt độ đặt ≤ ngưỡng tắt,
ngưỡng bật cao hơn ngưỡng tắt ít nhất 0,1°C và không vượt cảnh báo nhiệt cao.
Phần trăm là thời gian bật trong chu kỳ, không phải tốc độ quay của quạt.
Khi bật chế độ theo ngày ấp, lịch thông gió cũ được tắt theo logic web hiện có.

Web nhận cấu hình thật trước khi cho lưu. Máy cũ thiếu đủ trường thông gió
theo ngày vẫn chỉnh được ngưỡng nhiệt; phần chưa hỗ trợ bị khóa và có giải
thích. Web không tự thêm các trường mới hoặc tắt lịch cũ trên máy đó.

Tiêu đề trình duyệt: **MAYAP · Máy ấp trứng**. Các hướng dẫn Wi-Fi, cập
nhật, bộ nhớ và kết nối dùng từ “máy”, “màn hình máy”, “phần mềm máy”.
PIN ghép nối/PIN hiện tại tiếp tục nhận 4–8 số để tương thích máy đã có;
PIN mới phải 6–8 số, khớp backend. Không đổi quy tắc xác thực backend.

Giao diện sáng dùng nền xám xanh, thẻ trắng và teal; giao diện tối dùng
nền graphite và điểm nhấn teal. Màu chữ và màu nền nút tách riêng để giữ
tương phản. Biểu tượng trợ giúp/cảnh báo dùng SVG mask thay ký tự font.
Các nút trợ giúp có vùng chạm 44px; trạng thái đang gửi/chờ xác nhận hiển
thị tại form. Chế độ theo hệ thống, giảm chuyển động và zoom vẫn hoạt động.

Trên mobile, vuốt ngang sang tab bên cạnh theo thứ tự Thiết bị → Mẻ ấp →
Cài đặt, vuốt ngược để quay lại. Không vòng từ tab cuối sang tab đầu.
Vuốt dọc, vuốt tại ô nhập/nút/summary, thao tác nhiều ngón, mép màn hình
và hộp thoại không đổi tab. Chuyển tab giữ lại nội dung đang sửa và không
tự gửi cài đặt.

## Vì sao mở web/ghép nối chậm và phần đã cải thiện

| Điểm chờ | Thay đổi |
|---|---|
| Thêm máy xong tự tải lại trang sau 400ms, phải xin phiên và kết nối lần nữa | Bỏ reload; dùng credential vừa xác thực, giữ trang và mở/tái sử dụng kết nối hiện tại |
| Thư viện MQTT từ CDN ngoài | Đóng gói nguyên bản MQTT.js 5.13.2, có license; cache vendor theo phiên bản web |
| Script tải nối tiếp và chặn parser | `defer` theo đúng thứ tự, tải song song và chạy sau khi đọc HTML |
| 7 lời gọi SUBSCRIBE và SUBACK riêng | Gộp thành một SUBSCRIBE, giữ nguyên 7 topic/QoS và kiểm tra đủ grant; gộp yêu cầu trùng |
| Bản tin xin đồng bộ QoS0 đầu tiên bị mất | Thử thêm ở 700ms và 1600ms, chỉ khi còn thiếu snapshot/config; giữ chu kỳ hiện có 3 giây |
| SUBACK mất hoặc trả sau đổi kết nối | Timeout 8 giây, thử lại sau 3 giây; không nhận grant từ kết nối cũ/đã timeout |
| API cấp phiên lỗi tạm thời lúc mở trang | Timeout 10 giây cho yêu cầu cấp phiên, retry nền với backoff 5/10/20/30 giây; thử lại khi Internet trở về |
| Ghép nối hết hạn/thu hồi | HTTP 401/403 dừng retry tự động, yêu cầu ghép nối lại bằng PIN |

Bảy SUBSCRIBE cũ được gửi song song, vì vậy gộp packet không có nghĩa là
tăng tốc bảy lần. Thời gian xác thực, DNS/TLS/WebSocket/MQTT và phản hồi
thật từ máy vẫn phụ thuộc Internet, máy chủ và Wi-Fi. Không bỏ xác thực
hoặc báo trực tuyến chỉ dựa vào retained presence. Không thể hứa độ trễ
bằng 0 hoặc một thời gian cố định cho mọi mạng.

## Bảo toàn giao dịch và kiểm thử

MQTT topics/protocol, request ID, revision, HMAC, TTL, retry cùng signed
envelope, ACK cuối, UNCERTAIN và đối chiếu báo cáo bộ nhớ giữ nguyên.
Không coi PUBACK/received là lưu thành công. Credential MQTT và khóa
điều khiển vẫn nằm trong RAM; không ghi password xuống localStorage.
Timeout HTTP chỉ thêm cho cấp phiên kết nối/điều khiển, không áp cho API
đổi PIN/đổi tên hoặc thay đổi quy tắc giao dịch ghi cài đặt.

Manifest bảo toàn cũ đóng băng toàn bộ `app.js`, bao gồm cả giao diện.
Với phạm vi chỉnh web đã được yêu cầu, nó được thay bằng hash các hàm
giao dịch quan trọng từ `a60fe7e`; chỉ cho phép các thay đổi câu chữ được
liệt kê chính xác. Các kiểm thử giao dịch cũ vẫn giữ assertion hiện có.
Hash firmware/Boot/safety/schema/protocol vẫn giữ nguyên.

- `node --test tests/*.test.cjs`: 50/50 bài tại thời điểm sửa.
- Các kiểm tra release, ATtiny, reliability, EEPROM history và secrets qua.
- Browser QA Chrome thật: 320/390/430/768/1440px, sáng/tối; ba tab không
  tràn ngang; vuốt touch, cuộn dọc, nhập liệu, hộp thoại và giữ nội dung sửa.
- Kiểm tra API 503, mất sync đầu, pairing không reload, một MQTT client,
  credential không nằm trong native persistent storage và theme theo hệ thống.
- Tương phản các cặp màu chữ/nền và nút chính đã kiểm tra ≥4,5:1 ở hai theme.
- Các workflow hiện có tiếp tục chạy test C++/ASan/UBSan và compile ESP32 + ATtiny.

Browser QA dùng fixture và mock API/MQTT; không liên lạc hoặc điều khiển
máy thật. Khoảng 700ms đo được là thời điểm **retry sync**, không phải thời
gian ghép nối đầu-cuối của phần cứng. Chưa đo P50/P95 trên máy thật hoặc
kiểm tra thao tác bằng iPhone/Safari thật; cần nghiệm thu đó trước thương mại.

Chạy lại browser QA: phục vụ repo tại `http://127.0.0.1:8765` bằng HTTP
server cục bộ, cài Playwright bên ngoài repo, rồi chạy
`node tools/test_web_experience.cjs <thư_mục_ảnh>`.
`MAYAP_PLAYWRIGHT` chỉ đường dẫn package nếu không có trong node_modules;
`MAYAP_CHROME` chỉ executable Chrome tùy máy. Trên Windows mặc định dùng
Chrome đã cài; trên Linux dùng Chromium của Playwright.

## File thay đổi

`index.html`, `styles.css`, `app.js`, `config.js`, `sw.js`,
`manifest.webmanifest`, `release-manifest.json`, `README.md`,
`vendor/mqtt.min.js`, `vendor/mqtt-LICENSE.md`, `vendor/README.md`,
`tests/web-experience.test.cjs`, `tools/test_web_experience.cjs`,
`tests/runtime-preservation.json`, `tests/runtime-recovery.test.cjs`,
`tests/transaction-lifecycle.test.cjs` và tài liệu này.

Tham khảo API đã dùng: [MQTT.js 5.13.2](https://github.com/mqttjs/MQTT.js/blob/v5.13.2/README.md),
[Pointer Events](https://developer.mozilla.org/en-US/docs/Web/API/Pointer_events),
[touch-action](https://developer.mozilla.org/en-US/docs/Web/CSS/Reference/Properties/touch-action).
