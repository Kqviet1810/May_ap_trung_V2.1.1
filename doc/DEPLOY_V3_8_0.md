# MAYAP v3.8.0 — triển khai hardening

## Trải nghiệm khách hàng

Khách chỉ nhập mã máy và PIN. Không nhập hostname, cổng, WSS, tài khoản
MQTT, Cloudflare token hay Account ID.

Sau khi PIN hợp lệ, Worker cấp JWT MQTT một giờ, giới hạn vào đúng topic của
máy đó. Dashboard tự làm mới phiên bằng pairing token đã nhận sau xác thực.

## Cấu hình EMQX

Broker phải bật:

1. WSS tại hostname cố định.
2. JWT authentication HS256 với secret trùng Worker secret `MQTT_JWT_SECRET`.
3. Authorization từ claim `acl`.
4. Từ chối anonymous.
5. Từ chối mọi publish/subscribe không khớp ACL.
6. Giới hạn một client ID thiết bị và ghi log đăng nhập thất bại.

Worker cần các secret:

- `MAYAP_MQTT_HOST`
- `MAYAP_MQTT_WSS_URL`
- `MQTT_JWT_SECRET`
- Các secret VAPID và `DEVICE_KEY_PEPPER` hiện có.

Các giá trị này là cấu hình quản trị một lần, không phải dữ liệu khách nhập.

## GitHub Actions secret cho firmware

- `MAYAP_MQTT_HOST`
- `MAYAP_MQTT_USERNAME`
- `MAYAP_MQTT_PASSWORD`
- `MAYAP_DEVICE_SECRET` (chỉ dùng trong giai đoạn chuyển đổi thiết bị cũ)
- `MAYAP_OTA_PASSWORD` (để trống nếu không cần ArduinoOTA)
- `MAYAP_TLS_ROOT_CA`
- `MAYAP_OTA_SIGNING_PUBLIC_KEY`
- `MAYAP_OTA_SIGNING_PRIVATE_KEY`

Release bị chặn nếu thiếu cấu hình bảo mật bắt buộc.

## OTA ký số

Khóa riêng chỉ đặt trong GitHub Actions. Workflow ký file .bin bằng ECDSA
SHA-256 và phát hành file .sig. Worker từ chối release thiếu chữ ký. ESP32 tự
xác minh chữ ký bằng public key trước `Update.end(true)`.

## D1

Chạy:

```sh
cd cloudflare
npx wrangler d1 migrations apply mayap_push --remote
```

sau đó mới deploy Worker. Migration thêm rate limit PIN và chữ ký OTA.

## Kiểm thử bắt buộc

- ATtiny không phản hồi trong ít nhất ba lần retry: ESP32 không reset.
- Reset/thay nguồn ATtiny giữa mẻ: trạng thái mẻ được phục hồi.
- Sai PIN sáu lần: Worker trả 429.
- Không có pairing token: Worker từ chối đăng ký Push.
- JWT máy A không đọc hoặc ghi topic máy B.
- Chứng chỉ giả/hết hạn: ESP32 từ chối kết nối.
- Firmware đúng checksum nhưng sai chữ ký: OTA bị hủy.
- Mất cảm biến/nhiệt cao: SSR và contactor cùng OFF.
- SSR dính: thermostat cơ độc lập phải cắt contactor.

## Hạn chế phần cứng cần xử lý

ATtiny13A hiện không có đường đo riêng cho pin CR2032 của chính nó. Không thể
khắc phục triệt để chỉ bằng firmware mà vẫn đo đúng dưới tải. Bản PCB sau cần
một đường ADC phù hợp hoặc IC giám sát pin. Chi tiết an toàn nhiệt nằm tại
`doc/SAFETY_HARDWARE_REQUIREMENTS.md`.
