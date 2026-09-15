# MAYAP — Máy ấp trứng thông minh v3.8.0

> Firmware ESP32-S3, HMI tại máy, dashboard PWA và Cloudflare Worker cho hệ
> thống điều khiển/giám sát máy ấp trứng công nghiệp.

![Firmware](https://img.shields.io/badge/firmware-v3.8.0-0d8275)
![HMI](https://img.shields.io/badge/HMI-v3.8.0-0d8275)
![Web UI](https://img.shields.io/badge/web%20UI-v3.8.0-0d8275)
![Platform](https://img.shields.io/badge/platform-ESP32--S3-informational)
![CI](https://img.shields.io/badge/CI-firmware%20%2B%20web%20%2B%20worker-blue)

## Kiến trúc

Hệ thống có ba khối độc lập nhưng dùng chung Device ID `MAP-XXXXXXXXXXXX`:

1. `MAYAP_INDUSTRIAL_v3_4_0/`: firmware ESP32-S3 điều khiển PID nhiệt, đảo
   trứng, quạt, đèn, còi, lưu EEPROM/NVS, HMI và các interlock an toàn. Tên
   thư mục được giữ lại để không phá đường dẫn Arduino cũ; version thực nằm
   trong `config.h`.
2. `index.html`, `app.js`, `push.js`, `sw.js`: dashboard PWA giao tiếp trực
   tiếp với thiết bị qua MQTT over WebSocket.
3. `cloudflare/`: Worker + D1 nhận heartbeat/cảnh báo HTTPS, quản lý PIN và
   phát Web Push. Worker cũng làm cổng tải firmware từ GitHub Releases.

Luồng điều khiển thời gian thực không đi qua Worker. Luồng cảnh báo vẫn hoạt
động khi dashboard đóng; cảnh báo mất kết nối được Cron của Worker suy ra từ
heartbeat gần nhất.

## Cấu trúc chính

| Đường dẫn | Vai trò |
| --- | --- |
| `MAYAP_INDUSTRIAL_v3_4_0/config.h` | Version, GPIO, cấu hình build, giới hạn an toàn |
| `machine_control.h` | State machine, PID, lỗi, EEPROM/NVS và interlock |
| `hmi.h` | LCD ST7567S, encoder, buzzer và luồng thao tác tại máy |
| `network_service.h` | Wi-Fi, captive portal và trạng thái kết nối |
| `realtime_link.h` | Giao thức MQTT `mayap/v1`, snapshot/config/lệnh/ACK |
| `cloud_alert_link.h` | Đăng ký, heartbeat và cảnh báo HTTPS |
| `ota_update.h` | ArduinoOTA bảo trì trong LAN — mặc định tắt |
| `ota_web_update.h` | OTA từ xa có xác minh version, kích thước và SHA-256 |
| `config.js` | Cấu hình web mặc định fail-closed, không có broker/secret |
| `config.test.example.js` | Ví dụ riêng cho thử nghiệm có giám sát |
| `config.production.example.js` | Khung cấu hình web production |
| `cloudflare/schema.sql` | Schema D1, gồm chống brute-force PIN |
| `scripts/check_consistency.py` | Chặn lệch version, giao thức, mã lỗi, asset và schema |

## Build firmware

Board đã xác nhận: **ESP32S3 Dev Module**, flash thật 8 MB, không PSRAM.

```text
FQBN: esp32:esp32:esp32s3:PartitionScheme=default_8MB,FlashSize=8M,PSRAM=disabled
ESP32 Arduino core: 3.3.11
U8g2: 2.36.19
PubSubClient: 2.8
ArduinoJson: 7.4.2
```

Mở `MAYAP_INDUSTRIAL_v3_4_0/MAYAP_INDUSTRIAL_v3_4_0.ino` trong Arduino IDE,
cài đúng các version trên rồi biên dịch. Build mặc định không chứa credential
và không tự kết nối ra Internet.

### Build production

1. Sao chép `MAYAP_INDUSTRIAL_v3_4_0/secrets.example.h` thành `secrets.h`.
2. Điền broker riêng, tài khoản MQTT có ACL tối thiểu, CA PEM, device secret
   ngẫu nhiên riêng từng máy và PIN xuất xưởng riêng đúng 6 chữ số.
3. Giữ `MAYAP_PRODUCTION_BUILD=1`, `MAYAP_ENABLE_ARDUINO_OTA=0` và
   `MAYAP_ALLOW_INSECURE_TLS=0`.
4. Biên dịch. `static_assert` sẽ dừng build nếu thiếu điều kiện bắt buộc.

`secrets.h` đã bị Git bỏ qua. Không đưa file này, firmware `.bin` production
hoặc ảnh tem có PIN vào issue/log công khai.

ArduinoOTA chỉ dành cho bản bảo trì nội bộ trong LAN. Muốn bật phải đặt đồng
thời `MAYAP_ENABLE_ARDUINO_OTA=1` và mật khẩu tối thiểu 12 ký tự; bản production
bị chặn bật cơ chế này.

## Dashboard web

1. Sao chép `config.production.example.js` thành `config.js` trong pipeline
   triển khai.
2. Điền URL broker WSS riêng và `cloudApiBase` của Worker.
3. Cấp cho trình duyệt credential/token chỉ được truy cập topic của thiết bị
   được phép. Không dùng broker công cộng cho máy thật.
4. Triển khai lên HTTPS (GitHub Pages, Cloudflare Pages hoặc tương đương).

`config.js` là mã công khai đối với trình duyệt. Không đặt mật khẩu quản trị
broker hoặc credential dùng chung cho mọi máy trong file này. Phương án phát
hành chính thức cần token ngắn hạn hoặc tài khoản browser có ACL rất hẹp do
hạ tầng MQTT cấp.

Người dùng thêm máy bằng Device ID và PIN in trên tem. Không còn PIN mặc định
dùng chung. Chức năng reset PIN trên HMI đưa PIN về đúng PIN xuất xưởng riêng
của máy đó.

## Cloudflare Worker

```bash
cd cloudflare
npm install
npm run check
npm test
npm run db:migrate:remote
npm run deploy
```

Xem `cloudflare/README.md` để cấu hình D1, VAPID, origin, pepper và migration.
Worker giới hạn JSON 16 KiB, kiểm tra chặt Device ID/subscription, khóa thử PIN
sau 5 lần sai, không trả chi tiết exception ở production và từ chối browser
không đúng `ALLOWED_ORIGIN`.

Trước lần khởi động đầu tiên của máy production, dùng lệnh
`npm run provision:sql` theo `cloudflare/README.md` để đưa hash Device ID,
device secret và PIN vào D1. Worker mặc định từ chối tự đăng ký một Device ID
lạ, tránh bị chiếm trước bằng cách đoán ID.

## Kiểm tra và phát hành

Chạy kiểm tra cục bộ không cần phần cứng:

```bash
python3 scripts/check_consistency.py
npm --prefix cloudflare run check
npm --prefix cloudflare test
```

GitHub Actions chạy các kiểm tra trên và biên dịch firmware ở mọi pull request,
nhánh `main`, nhánh `release/**` và khi chạy thủ công. Dependency firmware được
khóa version; file vượt khe OTA `0x330000` làm job thất bại.

CI chỉ tạo artifact `MAYAP-firmware-ci-*` fail-closed, không có credential và
không tự tạo GitHub Release. Không nhúng `secrets.h` vào một `.bin` rồi phát
hành công khai: secret có thể bị trích xuất từ firmware. Kênh GitHub OTA trong
Worker vì vậy mặc định tắt. Trước khi ban hành OTA production cần chọn một trong
hai kiến trúc: chuyển credential riêng từng máy sang vùng lưu trữ được bảo toàn
qua OTA, hoặc chuyển artifact sang kho riêng có xác thực.

## Nguyên tắc an toàn khi sửa

- Không thay đổi interlock nhiệt/đảo hoặc schema EEPROM chỉ dựa trên test web.
- Khi thêm `FaultCode`, phải đồng bộ dashboard và tài liệu; CI sẽ chặn nếu
  `FAULT_TITLES` thiếu mã.
- Khi thay đổi payload MQTT, tăng version giao thức nếu không tương thích và
  cập nhật cả firmware lẫn web.
- Thử mất cảm biến, quá nhiệt, kẹt hành trình, mất điện giữa lúc ghi và rollback
  OTA trên thiết bị thật trước khi ban hành.
- Tham khảo `audit/OPERATION_RECOVERY_MANUAL.md` và `audit/manual/manual.html`
  cho quy trình vận hành/khắc phục sự cố.
