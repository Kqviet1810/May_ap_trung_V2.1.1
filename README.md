# MAYAP - Máy ấp trứng thông minh

> Firmware ESP32-S3 + Dashboard web PWA + Backend Cloudflare Worker cho hệ thống điều khiển và giám sát máy ấp trứng công nghiệp, giao tiếp thời gian thực qua MQTT và cảnh báo qua Web Push.

![Firmware](https://img.shields.io/badge/firmware-v3.4.0-0d8275)
![HMI](https://img.shields.io/badge/HMI-v3.6.0-0d8275)
![Web UI](https://img.shields.io/badge/web%20UI-v9.0.3-0d8275)
![Platform](https://img.shields.io/badge/platform-ESP32--S3-informational)
![License](https://img.shields.io/badge/license-Private-lightgrey)

---

## Giới thiệu

MAYAP là hệ thống điều khiển máy ấp trứng chạy trên **ESP32-S3**, gồm 3 thành phần phối hợp với nhau:

1. **Firmware ESP32-S3** (`MAYAP_INDUSTRIAL_v3_4_0/`) - điều khiển nhiệt độ (PID), độ ẩm, đảo trứng, quạt tuần hoàn/thông gió, đèn, còi báo; hiển thị HMI (màn hình + encoder xoay) trực tiếp trên máy; đồng bộ trạng thái qua MQTT.
2. **Web Dashboard** (PWA tĩnh - `index.html`, `app.js`, `styles.css`) - theo dõi và điều khiển máy từ xa qua trình duyệt/điện thoại, kết nối trực tiếp tới broker MQTT (không qua backend trung gian cho luồng điều khiển).
3. **Cloudflare Worker backend** (`cloudflare/`) - dịch vụ đăng ký & gửi **Web Push** (thông báo đẩy) khi máy gặp sự cố, thay thế cho kênh Telegram trước đây.

Toàn bộ giao diện, log và tài liệu trong dự án đều bằng **tiếng Việt** (đối tượng sử dụng là người vận hành trại ấp trong nước).

## Mục lục

- [Kiến trúc hệ thống](#kiến-trúc-hệ-thống)
- [Cấu trúc thư mục](#cấu-trúc-thư-mục)
- [Bắt đầu nhanh](#bắt-đầu-nhanh)
  - [1. Nạp firmware cho ESP32-S3](#1-nạp-firmware-cho-esp32-s3)
  - [2. Chạy Web Dashboard](#2-chạy-web-dashboard)
  - [3. Triển khai Cloudflare Worker (tuỳ chọn)](#3-triển-khai-cloudflare-worker-tuỳ-chọn)
- [Tính năng chính](#tính-năng-chính)
- [An toàn & thiết kế điều khiển](#an-toàn--thiết-kế-điều-khiển)
- [Cấu hình](#cấu-hình)
- [Đóng góp / phát triển thêm](#đóng-góp--phát-triển-thêm)

## Kiến trúc hệ thống

```
┌─────────────────────┐        MQTT (WSS)        ┌──────────────────────┐
│   ESP32-S3 firmware   │◄────────────────────────►│    Web Dashboard      │
│  (FreeRTOS, HMI, PID) │        mayap/v1/*         │  (GitHub Pages PWA)  │
└──────────┬───────────┘                           └──────────┬───────────┘
           │ HTTPS (cảnh báo)                                  │ đăng ký nhận Push
           ▼                                                   ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                  Cloudflare Worker (cloud_alert_link.h ↔ push.js)        │
│         D1 database (device, subscription)  +  Web Push (VAPID)          │
└─────────────────────────────────────────────────────────────────────────┘
```

- **Điều khiển thời gian thực**: web ↔ ESP32 qua MQTT trực tiếp (không đi qua Worker) để độ trễ thấp nhất.
- **Cảnh báo khi mất kết nối**: ESP32 tự gửi HTTPS lên Worker; Worker phát Web Push tới mọi trình duyệt đã đăng ký cho thiết bị đó, kể cả khi không mở trang web.
- **Nhiều máy trên một dashboard**: dashboard quản lý danh sách nhiều thiết bị (ID + PIN riêng từng máy), chuyển đổi nhanh giữa các máy.

## Cấu trúc thư mục

```
├── MAYAP_INDUSTRIAL_v3_4_0/     # Firmware ESP32-S3 (Arduino IDE)
│   ├── MAYAP_INDUSTRIAL_v3_4_0.ino   # Entry point, task FreeRTOS
│   ├── config.h                      # Chân GPIO, hằng số, cấu trúc cấu hình/runtime
│   ├── machine_control.h             # Logic điều khiển, PID, an toàn, lỗi, EEPROM
│   ├── hmi.h                         # Màn hình LCD + encoder (giao diện tại máy)
│   ├── network_service.h             # WiFi + captive portal đổi mạng
│   ├── realtime_link.h               # Đồng bộ MQTT với dashboard
│   ├── cloud_alert_link.h            # Gửi cảnh báo lên Cloudflare Worker
│   ├── ota_update.h                  # Nạp firmware qua Wi-Fi bằng Arduino IDE (cùng mạng LAN)
│   └── ota_web_update.h              # Cập nhật firmware từ xa qua Cloudflare (xem admin-firmware.html)
│
├── index.html / app.js / styles.css  # Web Dashboard (PWA, chạy tĩnh trên GitHub Pages)
├── setup.html                        # Trang bật Web Push qua quét QR trên máy
├── admin-firmware.html               # Trang riêng (không trong menu) để đẩy firmware mới qua Cloudflare
├── push.js / sw.js                   # Đăng ký Web Push + Service Worker
├── config.js                         # Cấu hình broker MQTT + Worker cho môi trường test
├── config.production.example.js      # Mẫu cấu hình cho triển khai thương mại
├── manifest.webmanifest              # Cấu hình PWA (cài vào màn hình chính)
│
└── cloudflare/                  # Backend Worker + D1 cho Web Push
    ├── src/index.js                  # Route API (đăng ký thiết bị, nhận cảnh báo, gửi push)
    ├── src/auth.js                   # Xác thực Device ID + PIN
    ├── src/push.js                   # Gửi Web Push (VAPID)
    ├── src/db.js                     # Truy vấn D1
    ├── schema.sql                    # Schema D1
    └── README.md                     # Hướng dẫn deploy Worker chi tiết
```

## Bắt đầu nhanh

### 1. Nạp firmware cho ESP32-S3

Firmware viết cho **Arduino IDE** (không dùng PlatformIO):

1. Mở `MAYAP_INDUSTRIAL_v3_4_0/MAYAP_INDUSTRIAL_v3_4_0.ino` bằng Arduino IDE.
2. Cài board **ESP32** (Espressif) qua Board Manager, chọn đúng board ESP32-S3.
3. Cài các thư viện được `#include` trong `config.h`/`network_service.h` (LCD, MQTT client, v.v. - xem đầu các file `.h`).
4. Kiểm tra sơ đồ chân trong `config.h` (mục `PIN_OUT_*` / `PIN_IN_*`) khớp với phần cứng thực tế.
5. Biên dịch và nạp vào board.
6. Theo dõi Serial Monitor ở lần boot đầu để xác nhận `[BOOT] config=...` (đặc biệt quan trọng sau khi nâng cấp firmware làm thay đổi schema cấu hình EEPROM).

**Nạp lại firmware qua Wi-Fi (OTA), không cần cáp USB** (xem `ota_update.h`):

1. Nạp lần đầu qua USB như trên là dùng được OTA ngay, mật khẩu mặc định là `181020` (đặt trong `config.h`, macro `MAYAP_OTA_PASSWORD`). Muốn đổi riêng cho một bản build khác: build flag `-D MAYAP_OTA_PASSWORD=\"mat_khau_khac\"` (Arduino IDE: **Sketch > Compiler flags**, hoặc `arduino-cli` với `--build-property`); để trống flag/macro này sẽ **tắt hẳn** tính năng OTA.
2. Chuyển máy sang **ONLINE** và chờ kết nối Wi-Fi thành công (xem màn KẾT NỐI trên HMI).
3. Mở lại Arduino IDE trên máy tính **cùng mạng LAN**: **Tools > Port** sẽ xuất hiện thêm mục mạng dạng `mayap-industrial at <IP> (ESP32S3 Dev Module)` (cần Bonjour/mDNS trên máy tính - Windows cài kèm iTunes/Bonjour Print Services, macOS/Linux có sẵn). Chọn cổng đó rồi bấm Upload như bình thường, IDE sẽ hỏi mật khẩu OTA.
4. Khuyến nghị chỉ nạp OTA lúc máy **đang rảnh** (ngoài mẻ ấp): lúc ghi flash, cả hai lõi CPU tạm dừng vài mili giây mỗi lần - vô hại với máy rảnh, nhưng nên tránh trùng lúc đang kiểm soát nhiệt sát ngưỡng.

**Cập nhật firmware TỪ XA qua Cloudflare (không cần cùng mạng LAN)** - xem `ota_web_update.h`:

Khác với OTA-Arduino-IDE ở trên (bắt buộc cùng Wi-Fi), cách này đẩy firmware qua Internet - dùng khi máy đã lắp đặt ở xa, không tiện mang máy tính đến tận nơi. Máy tự kiểm tra bản mới mỗi vài giờ khi đang ONLINE, hiện mục **"Cập nhật firmware"** trên HMI (trong KẾT NỐI) khi có bản mới - người vận hành phải tự xác nhận trên máy mới thực sự tải về/nạp (không tự động). Thiết lập backend (tạo R2 bucket, đặt token quản trị) xem [`cloudflare/README.md`](cloudflare/README.md#cập-nhật-firmware-từ-xa-ota-qua-web); trang tải bản mới lên là [`admin-firmware.html`](admin-firmware.html).

### 2. Chạy Web Dashboard

Dashboard là site tĩnh, có thể chạy trực tiếp bằng cách mở `index.html`, hoặc deploy lên **GitHub Pages**:

1. Copy `config.production.example.js` thành `config.js`, chỉnh `mqttUrl` trỏ tới broker MQTT thật (khuyến nghị broker riêng cho môi trường thương mại, **không dùng broker công cộng**) và `cloudApiBase` trỏ tới Worker đã deploy (bước 3).
2. Bật GitHub Pages cho repo (hoặc host bằng bất kỳ static hosting nào - Cloudflare Pages, Netlify...).
3. Truy cập trang, bấm **+** để thêm thiết bị bằng Device ID + PIN hiển thị trên máy (mặc định `1111`, nên đổi ngay sau khi thêm).
4. Trên điện thoại, có thể "Thêm vào Màn hình chính" để dùng như app PWA, nhận thông báo đẩy kể cả khi không mở trình duyệt.

### 3. Triển khai Cloudflare Worker (tuỳ chọn)

Bắt buộc nếu muốn nhận **cảnh báo đẩy** (mất điện, mất mạng, lỗi cảm biến...) trên điện thoại. Xem hướng dẫn đầy đủ tại [`cloudflare/README.md`](cloudflare/README.md), tóm tắt:

```bash
cd cloudflare
npm install
npx wrangler d1 create mayap_push        # rồi dán database_id vào wrangler.toml
npm run db:migrate:remote
npx web-push generate-vapid-keys         # tạo cặp khoá VAPID
npx wrangler secret put VAPID_PUBLIC_KEY
npx wrangler secret put VAPID_PRIVATE_KEY
npx wrangler secret put VAPID_SUBJECT
npx wrangler secret put DEVICE_KEY_PEPPER
npm run deploy
```

## Tính năng chính

**Điều khiển & giám sát**
- Điều khiển nhiệt độ bằng PID (có Auto-Tune tự động tìm Kp/Ki/Kd), quản lý chu kỳ đảo trứng theo công tắc hành trình, quạt tuần hoàn/thông gió, đèn, còi báo.
- Màn hình HMI tại máy (LCD + encoder xoay) với màn hình khởi động, xem/chỉnh toàn bộ thông số không cần dashboard.
- Dashboard web theo dõi thời gian thực (nhiệt độ, độ ẩm, trạng thái từng đầu ra), chỉnh nhanh thông số vận hành, xem nhật ký mẻ ấp.
- Quản lý nhiều máy ấp trên cùng một dashboard, mỗi máy có Device ID + PIN riêng.

**Cảnh báo & thông báo**
- Hơn 20 loại cảnh báo: mất cảm biến, nhiệt độ cao/thấp/khẩn cấp, độ ẩm bất thường, bỏ lỡ chu kỳ đảo, mất điện/mất mạng kèm thông báo khi có điện trở lại, tín hiệu WiFi yếu kéo dài, xung đột output, lỗi EEPROM/RTC/I2C...
- Web Push tới điện thoại kể cả khi không mở trình duyệt (qua Cloudflare Worker + Service Worker), áp dụng cho mọi thiết bị trên dashboard.
- Cảnh báo nhiệt độ "thông minh": tạm im còi khi đã xác nhận và nhiệt độ đang giảm thật, tự kêu lại ngay nếu nhiệt độ ngừng giảm hoặc tăng trở lại.

**An toàn**
- Kiến trúc điều khiển nhiệt hai lớp độc lập: relay tổng theo công tắc vật lý + SSR điều khiển PID, cộng với watchdog/giám sát stack, phát hiện reset bất thường, khoá an toàn khi lưu trữ cấu hình không khả dụng.
- EEPROM lưu cấu hình có schema versioning, tự nâng cấp an toàn giữa các phiên bản firmware mà không mất cấu hình đã lưu.

## An toàn & thiết kế điều khiển

Một vài nguyên tắc thiết kế cố ý (đọc kỹ trước khi sửa `machine_control.h`):

- **Relay nhiệt tổng (`PIN_OUT_HEAT_MASTER`) theo công tắc vật lý là chính** - phần mềm chỉ được cảnh báo, ngoại trừ **một** trường hợp ngoại lệ được giữ lại có chủ đích: tự động ngắt khẩn cấp khi quá nhiệt, làm lớp bảo vệ dự phòng cho trường hợp SSR bị kẹt/chập (một lỗi phần cứng thực tế đã ghi nhận).
- **Hai lớp an toàn nhiệt độc lập**: dòng điện thực tế ra thanh nhiệt cần *cả* relay tổng *và* SSR (điều khiển PID) cùng cho phép - mất một lớp không làm mất an toàn.
- Alarm nhiệt độ cao/khẩn cấp có thể cấu hình hoạt động độc lập với trạng thái mẻ ấp (mặc định bật, có thể tắt trong Cài đặt nếu chỉ muốn cảnh báo khi đang có mẻ).

## Cấu hình

| File | Vai trò |
| --- | --- |
| `config.js` | Cấu hình broker MQTT + Worker cho **môi trường test** (đang trỏ tới broker công cộng `broker.emqx.io` - **không dùng cho máy thương mại**). |
| `config.production.example.js` | Mẫu cấu hình cho triển khai thật, copy thành `config.js` và điền broker/Worker riêng. |
| `MAYAP_INDUSTRIAL_v3_4_0/config.h` | Hằng số firmware: chân GPIO, ngưỡng an toàn mặc định, kích thước task/stack. |
| `cloudflare/wrangler.toml` | Cấu hình Worker: `ALLOWED_ORIGIN` (origin của dashboard), `database_id` (D1). |

> Website chạy dưới dạng mã tĩnh (GitHub Pages) - bất kỳ giá trị nào đặt trong `config.js` (kể cả mật khẩu MQTT) đều có thể bị xem được từ trình duyệt. Với triển khai thương mại, ưu tiên dùng token ngắn hạn do backend cấp thay vì mật khẩu tĩnh.

## Đóng góp / phát triển thêm

- Firmware build bằng Arduino IDE, không có PlatformIO/CI biên dịch tự động trong repo này - kiểm tra kỹ trước khi nạp vào máy thật, đặc biệt các thay đổi liên quan an toàn nhiệt và schema EEPROM.
- Khi thêm trường mới vào cấu hình lưu EEPROM (`PackedMachineConfigV1` trong `machine_control.h`), phải tăng `CONFIG_SCHEMA` và bổ sung đường nâng cấp tương thích ngược (xem các khối `ConfigRecordLegacyV*` hiện có làm mẫu).
- Toàn bộ giao diện (HMI + web) dùng tiếng Việt không dấu ở tên biến/hằng số nhưng có dấu ở chuỗi hiển thị cho người dùng.
