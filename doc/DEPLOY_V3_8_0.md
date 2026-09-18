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

## Chuẩn bị firmware xuất xưởng

### ID và mật khẩu

- **ID máy không chỉnh tay**: ESP32 tạo ID dạng `MAP-<MAC>` từ eFuse MAC tại
  `MAYAP_INDUSTRIAL_v3_4_0/network_service.h`, hàm
  `mayapDeviceIdText()`. Đây là định danh duy nhất; không dùng cùng một ID
  cho hai máy.
- **Wi-Fi xưởng (tùy chọn)**: đặt GitHub Secrets
  `MAYAP_WIFI_SSID` và `MAYAP_WIFI_PASSWORD`. Workflow đưa hai giá trị này
  vào firmware. Bỏ trống nếu để nhân viên/khách cấu hình tại HMI.
- **Mật khẩu OTA (tùy chọn)**: GitHub Secret `MAYAP_OTA_PASSWORD`. Để trống
  thì ArduinoOTA bị tắt hoàn toàn.
- **PIN web** không đặt sẵn trong mã nguồn: Worker tạo PIN 6 số ngẫu nhiên
  khi máy đăng ký, ESP32 lưu NVS và hiển thị ở **Cài đặt → Thông tin kết nối**.
  Khi quên PIN, chọn **Đặt lại mã PIN** trên HMI để tạo PIN mới.
- Mật khẩu AP cấu hình tạm thời cũng được tạo ngẫu nhiên, hiển thị trên HMI;
  không dùng mật khẩu cố định trước khi xuất xưởng.

### Quy trình phát hành và nạp

1. Trong GitHub repository, vào **Settings → Secrets and variables → Actions**
   và đặt các secret trong mục trên, cùng các secret bảo mật/MQTT ở phần trước.
2. Chạy workflow **Build & release firmware** bằng *Run workflow* để kiểm tra
   bản thử. Chỉ tạo release có chữ ký khi tạo tag phiên bản, ví dụ `v3.8.0`.
3. Nạp file `.bin` đã ký vào một máy pilot qua USB. Lần khởi động đầu,
   ESP32 gửi PING để xác nhận ATtiny sẵn sàng; ATtiny không còn đo/báo pin.
4. Tại HMI, chọn Online rồi kiểm tra Wi-Fi, ID máy và PIN Web. Trên web, thêm
   máy bằng đúng ID + PIN; không nhập WSS hay tài khoản MQTT.
5. Test tối thiểu: khởi động lại ESP32, ngắt ATtiny, mất Wi-Fi, mất cảm biến,
   và thử một gói OTA sai chữ ký. Khi tất cả đạt mới nạp hàng loạt.

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

Tính năng đo/báo pin ATtiny đã được vô hiệu hóa theo cấu hình vận hành hiện
tại. ESP32 chỉ dùng PING/ACK để xác nhận ATtiny sẵn sàng sau khi khởi động và
trong lúc đang có mẻ. Chi tiết an toàn nhiệt nằm tại
`doc/SAFETY_HARDWARE_REQUIREMENTS.md`.
