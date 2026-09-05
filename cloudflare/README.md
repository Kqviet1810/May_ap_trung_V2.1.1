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
npx wrangler secret put GITHUB_TOKEN       # TUY CHON - xem "Cap nhat firmware tu xa" ben duoi
```

## Sua wrangler.toml

- `ALLOWED_ORIGIN`: dung origin GitHub Pages cua ban (vi du `https://ten-user.github.io`).
- `database_id`: id D1 vua tao o buoc tren.

## Cap nhat firmware tu xa (qua GitHub Releases)

Cho phep thiet bi dang ONLINE tu kiem tra + tai firmware moi TU XA (khong
can cung mang LAN nhu OTA qua Arduino IDE) - xem `ota_web_update.h` phia
firmware va `.github/workflows/build-firmware.yml` o repo goc.

Nguon duy nhat la GitHub Releases cua chinh repo nay - khong can trang quan
tri/upload thu cong, khong can R2:

1. Sua code nhu binh thuong, commit + push (Claude Code hoac ban tu lam).
2. Khi san sang phat hanh 1 ban firmware moi, day 1 tag dang `vX.Y.Z` (khop
   `MAYAP_FIRMWARE_VERSION` trong `config.h`):
   ```bash
   git tag v3.5.0
   git push origin v3.5.0
   ```
3. GitHub Actions tu dong bien dich (arduino-cli tren may chu GitHub, co
   internet day du) va tao 1 GitHub Release moi voi file `.bin` dinh kem -
   khong ai phai tu tay build/upload.
4. Worker tu hoi GitHub Releases API khi co thiet bi/trinh duyet hoi phien
   ban, TU TAI va TU BAM LAI SHA-256 that su cua file .bin (khong tin bat ky
   checksum co san nao), cache ket qua trong D1 (bang `firmware_cache`,
   lam moi moi 10 phut) de khong phai tai lai lien tuc.
5. `GITHUB_TOKEN` (secret o tren) la TUY CHON - khong dat van hoat dong binh
   thuong (goi GitHub API khong xac thuc, gioi han 60 request/gio - du dung
   cho vai thiet bi); dat 1 Personal Access Token (khong can quyen gi dac
   biet, chi doc public repo) neu co nhieu thiet bi de nang gioi han len
   5000 request/gio.
6. Thiet bi dang ONLINE tu kiem tra moi 6 gio; nguoi dung cung co the bam
   nut "Cap nhat" trong dashboard web (yeu cau kiem tra ngay qua MQTT).
   CA HAI truong hop deu chi hien "Cap nhat firmware" tren HMI (muc KET
   NOI) - nguoi van hanh phai tu xac nhan tai may moi thuc su tai ve/nap,
   khong bao gio tu dong ngoai y muon.

## Chay thu local

```bash
npm run dev
```

## Deploy

```bash
npm run deploy
```
