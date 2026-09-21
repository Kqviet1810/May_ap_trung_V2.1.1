# MAYAP — Máy ấp trứng thông minh

> Baseline hiện hành: **MAYAP release 3.8.1** trên ESP32-S3-WROOM-1U-N8, Web PWA và Cloudflare Worker.

| Thành phần | Phiên bản hiện hành |
|---|---:|
| Release | 3.8.1 |
| ESP32 firmware | 3.8.1 |
| HMI firmware | 3.7.0 |
| Web cache | 11.7.3 |
| ATtiny protocol | 2 |
| ESP32 Arduino core CI | 3.3.11 |
| Arduino CLI CI | 1.5.1 |
| Node CI | 24 |

Các giá trị trên được khai báo ở `release-manifest.json` và được CI đối chiếu với source bằng `tools/check_release_sync.py`. Không sửa một phiên bản đơn lẻ mà không cập nhật manifest/checker tương ứng.

## Kiến trúc hiện hành

```text
                 MQTT/TLS + WSS
ESP32-S3  <---------------------------->  Web PWA
   |                                          |
   | HTTPS/TLS                                | HTTPS
   v                                          v
Cloudflare Worker + D1  ---------------->  Web Push
   |
   +--> GitHub Releases (metadata + firmware OTA)

ESP32-S3 <---- pulse protocol v2 ----> ATtiny13A
```

- **Điều khiển thời gian thực:** Web ↔ ESP32 qua MQTT. Network I/O không chạy trong `controlTask`.
- **Lệnh ghi MQTT:** Web chỉ nhận broker credential sau khi phiên trình duyệt đã xác thực; các kênh ghi quan trọng còn được ký HMAC riêng theo thiết bị.
- **Cloud:** Worker đảm nhiệm provisioning, PIN/session, Web Push, trạng thái online/offline và metadata OTA.
- **OTA Internet:** chỉ kiểm tra/tải firmware sau khi operator xác nhận tại HMI; ESP32 kiểm SHA-256 và chữ ký ECDSA trước khi nạp.
- **An toàn nhiệt:** firmware không phải lớp bảo vệ duy nhất. Phần cứng phải có thermostat/thermal relay độc lập, contactor an toàn và thermal fuse theo `doc/SAFETY_HARDWARE_REQUIREMENTS.md`.

Chi tiết: `doc/ARCHITECTURE_V3_8_1.md`.

## Trải nghiệm người dùng

Người dùng cuối **không nhập hostname, port, WSS, MQTT username/password hay Cloudflare token**. Luồng chuẩn là:

1. Máy tạo Device ID dạng `MAP-XXXXXXXXXXXX` từ eFuse MAC.
2. ESP32 có device key 256-bit riêng, lưu NVS.
3. Máy đăng ký/provision qua Worker và đồng bộ PIN Web.
4. Trên web, người dùng thêm máy bằng **Device ID + PIN**.
5. Worker cấp browser session; dashboard tự lấy cấu hình MQTT cần thiết cho phiên đã xác thực.

Nếu `REQUIRE_DEVICE_INVENTORY=0`, Worker v3.8.1 có thể auto-admit máy mới theo rate-limit và ghi vào `device_inventory`. Nếu đặt `=1`, quay lại chế độ factory allowlist nghiêm ngặt.

## Cấu trúc repo

```text
MAYAP_INDUSTRIAL_v3_4_0/   ESP32 firmware
ATTINY13A_POWER_ALARM/     firmware ATtiny13A
cloudflare/                Worker + D1 migrations
.github/workflows/         CI/build/release/deploy
app.js/config.js/...       Web PWA
release-manifest.json      manifest đồng bộ release
tools/                     regression/sync checker
doc/                       kiến trúc, deploy, commissioning, safety
audit/                     audit lịch sử + delta audit
```

Tên thư mục firmware `MAYAP_INDUSTRIAL_v3_4_0` là tên sketch lịch sử; **không đại diện phiên bản firmware hiện tại**. Phiên bản thật nằm ở `MAYAP_FIRMWARE_VERSION` và `release-manifest.json`.

## Build ESP32

Board: **ESP32-S3-WROOM-1U-N8**, flash thật 8 MB, không PSRAM.

Thiết lập bắt buộc:

- Flash Size: 8 MB
- Partition Scheme: `default_8MB` / “8M with spiffs”
- PSRAM: disabled
- Không dùng `huge_app` vì cấu hình đó không có dual OTA phù hợp dự án.

CI dùng FQBN:

```text
esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,PartitionScheme=default_8MB,FlashSize=8M,PSRAM=disabled
```

### Build profile

Workflow hiện chia 3 profile mà không cần sửa source:

- **DEV:** diagnostic Serial ON, input simulation OFF.
- **PILOT:** chạy `workflow_dispatch`, diagnostic Serial ON, input simulation OFF.
- **PROD:** build từ tag `vX.Y.Z`, diagnostic Serial OFF, input simulation OFF.

Tag release phải khớp chính xác `MAYAP_FIRMWARE_VERSION`.

## CI/release invariants

Trước build/release, pipeline bắt buộc:

1. kiểm version/tag;
2. cấm `setInsecure()`;
3. cấm private key trong firmware;
4. kiểm contract ESP32 ↔ ATtiny;
5. chạy `tools/check_release_sync.py`;
6. chạy `tools/check_v381_reliability.py`;
7. syntax-check toàn bộ entrypoint JS, gồm cả security/reliability wrapper;
8. compile ATtiny với giới hạn 1 KB flash / 64 B static RAM;
9. compile ESP32;
10. với tag: ký ECDSA và tạo GitHub Release.

CI dùng Node 24. Arduino CLI được tải từ GitHub Release chính thức ở phiên bản cố định và kiểm SHA-256 trước khi cài, không phụ thuộc `arduino/setup-arduino-cli@v2`.

## Cloudflare

Entrypoint hiện hành là:

```text
cloudflare/src/reliability-wrapper.js
  -> security-wrapper.js
     -> index.js
```

`wrangler.toml` hiện giữ compatibility date `2026-08-01` và `nodejs_compat` có chủ ý. Không tự động đẩy compatibility date theo ngày hiện tại khi chưa regression-test Worker.

Deploy: `.github/workflows/deploy-cloudflare-worker.yml`.

Hiện `cloudflare/` chưa commit `package-lock.json`, vì vậy workflow dùng `npm install` với dependency trực tiếp đã pin và phát warning. Khi lockfile được tạo/commit hợp lệ, workflow tự chuyển sang `npm ci`.

Chi tiết: `cloudflare/README.md` và `doc/DEPLOY_V3_8_1.md`.

## OTA

Có hai đường OTA độc lập:

- **ArduinoOTA LAN:** chỉ dùng khi đặt `MAYAP_OTA_PASSWORD`; để trống là tắt.
- **Internet OTA:** GitHub Release → Worker → ESP32; không tự flash từ xa. Operator phải xác nhận tại máy.

Private signing key chỉ được đặt trong GitHub Actions secret. Firmware chỉ chứa public key xác minh.

## Tài liệu chuẩn

- `doc/ARCHITECTURE_V3_8_1.md` — source-of-truth kiến trúc.
- `doc/DEPLOY_V3_8_1.md` — triển khai hiện hành.
- `doc/COMMISSIONING_V3_8_1.md` — checklist máy pilot và soak.
- `doc/SAFETY_HARDWARE_REQUIREMENTS.md` — yêu cầu an toàn phần cứng.
- `doc/DEPLOY_V3_8_0.md` — **archive, không dùng để triển khai**.
- `audit/06_V381_DELTA_AUDIT.md` — trạng thái delta audit v3.8.1.

## Nguyên tắc source-of-truth

Khi comment/tài liệu cũ mâu thuẫn với code thực thi, phải xác minh lại và sửa tài liệu; không dùng comment cũ để thay đổi hành vi an toàn đang chạy. Với heater safety, provisioning, MQTT auth, ATtiny và OTA, mọi thay đổi phải đi qua regression gate và commissioning trước khi phát hành hàng loạt.
