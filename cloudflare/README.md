# MAYAP Push Worker

Backend Cloudflare Worker + D1 cho kenh thong bao ESP32 -> Web Push (thay the Telegram).
Huong dan day du (VAPID, secrets, D1, deploy, test) nam trong bao cao chinh cua phien lam viec -
file nay chi tom tat lenh de chay nhanh.

## Cai dat

```bash
cd cloudflare
npm install
```

## Tao D1 database

```bash
npx wrangler d1 create mayap_push
# Dan database_id tra ve vao wrangler.toml (o [[d1_databases]])
npm run db:migrate:remote
```

## Tao VAPID keys

```bash
npx web-push generate-vapid-keys
```

## Dat secrets (KHONG dua vao wrangler.toml)

```bash
npx wrangler secret put VAPID_PUBLIC_KEY
npx wrangler secret put VAPID_PRIVATE_KEY
npx wrangler secret put VAPID_SUBJECT      # vi du: mailto:ban@example.com
npx wrangler secret put DEVICE_KEY_PEPPER  # chuoi ngau nhien dai, tu sinh 1 lan
npx wrangler secret put ADMIN_UPLOAD_TOKEN # chuoi ngau nhien dai - xem "Cap nhat firmware tu xa" ben duoi
```

## Sua wrangler.toml

- `ALLOWED_ORIGIN`: dung origin GitHub Pages cua ban (vi du `https://ten-user.github.io`).
- `database_id`: id D1 vua tao o buoc tren.

## Cap nhat firmware tu xa (OTA qua web)

Cho phep day firmware moi den moi thiet bi dang ONLINE tu 1 trang quan tri
rieng (`admin-firmware.html` o repo goc), khong can cung mang LAN nhu OTA
qua Arduino IDE - xem `ota_web_update.h` phia firmware.

1. Tao R2 bucket luu file `.bin`:
   ```bash
   npx wrangler r2 bucket create mayap-firmware
   ```
2. Dat `ADMIN_UPLOAD_TOKEN` (xem lenh `wrangler secret put` o tren) - chuoi
   bi mat CHI MINH BAN biet, KHONG chia se/commit. Ai co token nay day duoc
   firmware len MOI thiet bi.
3. Mo `admin-firmware.html` (deploy cung noi voi dashboard chinh, hoac mo
   thang tu may tinh ca nhan - trang khong nam trong menu, chi ban tu vao),
   nhap dia chi Worker + token vua dat, tai file `.bin` len + dat phien ban
   (dung dinh dang `X.Y.Z`, phai KHOP `MAYAP_FIRMWARE_VERSION` trong
   `config.h` cua ban build do).
4. Bam "Dat lam moi nhat" khi san sang phat hanh - CHI luc do thiet bi moi
   thay va tai duoc (upload xong van la "ban nhap", chua anh huong gi).
5. Thiet bi dang ONLINE se tu kiem tra moi vai gio, hien "Cap nhat firmware"
   tren HMI (muc KET NOI) - nguoi van hanh phai tu xac nhan tren may moi
   thuc su tai ve/nap, khong tu dong ngoai y muon.

## Chay thu local

```bash
npm run dev
```

## Deploy

```bash
npm run deploy
```
