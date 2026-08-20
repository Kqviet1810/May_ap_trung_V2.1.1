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
```

## Sua wrangler.toml

- `ALLOWED_ORIGIN`: dung origin GitHub Pages cua ban (vi du `https://ten-user.github.io`).
- `database_id`: id D1 vua tao o buoc tren.

## Chay thu local

```bash
npm run dev
```

## Deploy

```bash
npm run deploy
```
