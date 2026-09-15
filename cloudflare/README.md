# MAYAP Push Worker

Cloudflare Worker + D1 cho đăng ký thiết bị, PIN, heartbeat, Web Push và cổng
OTA. Luồng điều khiển thời gian thực vẫn đi trực tiếp qua MQTT.

## Cài đặt

```bash
cd cloudflare
npm install
npm run check
npm test
npx wrangler d1 create mayap_push
# Dán database_id được trả về vào wrangler.toml
npm run db:migrate:remote
```

Tạo VAPID key bằng công cụ tin cậy, sau đó lưu toàn bộ bí mật bằng Wrangler;
không đặt giá trị thật trong `wrangler.toml`:

```bash
npx wrangler secret put VAPID_PUBLIC_KEY
npx wrangler secret put VAPID_PRIVATE_KEY
npx wrangler secret put VAPID_SUBJECT
npx wrangler secret put DEVICE_KEY_PEPPER
npx wrangler secret put PIN_RATE_LIMIT_PEPPER
npx wrangler secret put GITHUB_TOKEN          # tuỳ chọn, tăng hạn mức gọi GitHub
```

- `DEVICE_KEY_PEPPER`: chuỗi ngẫu nhiên tối thiểu 32 ký tự; không được đổi sau
  khi đã có dữ liệu nếu chưa có kế hoạch băm lại toàn bộ key/PIN.
- `PIN_RATE_LIMIT_PEPPER`: chuỗi ngẫu nhiên riêng dùng băm định danh client.
  Nếu bỏ trống, Worker dùng `DEVICE_KEY_PEPPER`.
- `ALLOWED_ORIGIN`: đặt đúng origin HTTPS của dashboard, không có `/` cuối.
  Request browser từ origin khác sẽ bị từ chối; ESP32 không gửi `Origin` nên
  không bị ảnh hưởng.
- `PUSH_ENDPOINT_HOST_SUFFIXES`: chỉ bổ sung khi nhà cung cấp Push hợp lệ không
  thuộc danh sách Google/Mozilla/Apple/Microsoft mặc định; review hostname trước.

Để chạy local, sao chép `.dev.vars.example` thành `.dev.vars`, điền giá trị test
và chạy `npm run dev`.

## D1 và nâng cấp cơ sở dữ liệu

`schema.sql` tạo mới đầy đủ các bảng và có thể chạy lại để bổ sung bảng/index
dùng `IF NOT EXISTS`. Với database rất cũ, kiểm tra cột trước:

```bash
npx wrangler d1 execute mayap_push --remote \
  --command="PRAGMA table_info(devices)"
```

Nếu thiếu, chạy mỗi lệnh đúng một lần:

```sql
ALTER TABLE devices ADD COLUMN batch_running INTEGER NOT NULL DEFAULT 0;
ALTER TABLE devices ADD COLUMN web_pin_hash TEXT;
```

Sau đó chạy `npm run db:migrate:remote` để tạo `pin_attempts` và các index còn
thiếu. Thiết bị cũ có `web_pin_hash IS NULL` sẽ chưa chấp nhận PIN fallback;
lần đăng ký hợp lệ kế tiếp từ firmware v3.8.0 sẽ ghi PIN xuất xưởng riêng.

## Mô hình xác thực

- Device ID phải đúng `MAP-` + 12 ký tự hex viết hoa.
- Firmware xác thực bằng device secret ngẫu nhiên 32–128 ký tự. D1 chỉ lưu
  SHA-256 đã trộn pepper.
- Mỗi máy có PIN xuất xưởng riêng đúng 6 chữ số. Người dùng có thể đổi thành
  PIN 4–8 chữ số; không còn fallback `1111`.
- Production phải provision hash của Device ID/secret/PIN vào D1 trước khi máy
  khởi động lần đầu; Worker không tự tạo Device ID lạ. `ALLOW_TOFU_REGISTRATION=1`
  chỉ dành cho lab có giám sát.
- Đăng ký Push cần PIN hợp lệ hoặc pairing token cũ còn hiệu lực.
- Sau 5 lần sai PIN trong 10 phút, cặp thiết bị/client bị khóa 15 phút. IP chỉ
  được lưu dưới dạng hash có pepper.
- JSON request tối đa 16 KiB; Push endpoint phải là HTTPS; lỗi nội bộ không lộ
  chi tiết trừ khi chủ động đặt `DEBUG_ERRORS=1` trong môi trường test.

Provision một máy mà không đưa secret/PIN vào câu lệnh SQL hoặc D1:

```bash
export MAYAP_DEVICE_ID='MAP-XXXXXXXXXXXX'
export MAYAP_DEVICE_SECRET='chuoi-ngau-nhien-rieng-32-128-ky-tu'
export MAYAP_FACTORY_PIN='123456'
export DEVICE_KEY_PEPPER='dung-chinh-pepper-cua-worker'
npm run provision:sql > provision.sql
npx wrangler d1 execute mayap_push --remote --file=./provision.sql
```

`provision.sql` chỉ chứa hash nhưng vẫn là dữ liệu vận hành; xóa an toàn sau
khi chạy và không commit. Không đặt secret thật trực tiếp trên command line.

## Deploy

```bash
npm run db:migrate:remote
npm run deploy
npx wrangler tail
```

Sau deploy, kiểm tra tối thiểu: đăng ký thiết bị, thử PIN đúng/sai, khóa sau 5
lần sai, bật/tắt Push, gửi thông báo test, heartbeat và Cron đánh dấu offline.
Không dùng dữ liệu production cho kiểm thử phá khóa.

## OTA qua GitHub Releases

Firmware phát hành không chứa Wi-Fi/MQTT/device secret; toàn bộ dữ liệu riêng
từng máy nằm trong NVS `mayap_conn` và được giữ nguyên qua OTA. Vì vậy
`ENABLE_PUBLIC_GITHUB_OTA="1"` được bật mặc định trong `wrangler.toml`; đổi về
`"0"` và deploy lại nếu cần khóa cập nhật từ xa.

Workflow chỉ tạo Release từ tag `vX.Y.Z` thuộc nhánh `main`. Worker chỉ nhận asset tên chính xác
`MAYAP-firmware-X.Y.Z.bin`, tự tải và băm SHA-256, từ chối file rỗng hoặc lớn
hơn khe OTA `0x330000`, rồi cache metadata trong D1.
