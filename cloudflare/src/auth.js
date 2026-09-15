// Xac thuc thiet bi (ESP32) va tien ich lien quan - khong dung thu vien ngoai,
// chi Web Crypto co san tren Cloudflare Workers.

function toHex(buffer) {
  return [...new Uint8Array(buffer)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

// Bam device_key bang SHA-256 (co "pepper" bi mat cua worker tron vao truoc khi
// bam) - KHONG luu device_key dang plaintext trong D1. Day khong phai mat khau
// nguoi dung (khong can bcrypt/argon2 cham), la 1 chuoi bi mat co do ngau nhien
// cao do firmware tu sinh/duoc cap luc san xuat, nen SHA-256 + pepper la du.
export async function hashDeviceKey(deviceKey, pepper) {
  if (typeof deviceKey !== 'string' || !deviceKey ||
      typeof pepper !== 'string' || pepper.length < 32) {
    throw new Error('DEVICE_KEY_PEPPER phai co it nhat 32 ky tu');
  }
  const data = new TextEncoder().encode(`${pepper}:${deviceKey}`);
  const digest = await crypto.subtle.digest('SHA-256', data);
  return toHex(digest);
}

// So sanh 2 chuoi hex ve mat thoi gian co dinh (khong short-circuit theo do
// dai/ky tu dau tien khac nhau) - han che kieu tan cong do thoi gian phan hoi.
export function timingSafeEqual(a, b) {
  if (typeof a !== 'string' || typeof b !== 'string') return false;
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i += 1) {
    diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  }
  return diff === 0;
}

export async function verifyDeviceKey(deviceKey, pepper, storedHash) {
  if (!deviceKey || !storedHash) return false;
  const computed = await hashDeviceKey(deviceKey, pepper);
  return timingSafeEqual(computed, storedHash);
}

export function isValidDeviceId(id) {
  return typeof id === 'string' && /^MAP-[A-F0-9]{12}$/.test(id);
}

export function isValidFactoryPin(pin) {
  return typeof pin === 'string' && /^[0-9]{6}$/.test(pin);
}

export function isTrustedPushEndpoint(endpoint, env = {}) {
  try {
    const url = new URL(String(endpoint || ''));
    if (url.protocol !== 'https:' || url.username || url.password ||
        (url.port && url.port !== '443')) return false;
    const defaults = [
      'googleapis.com', 'mozilla.com', 'push.apple.com', 'notify.windows.com',
    ];
    const configured = String(env.PUSH_ENDPOINT_HOST_SUFFIXES || '')
      .split(',').map((item) => item.trim().toLowerCase()).filter(Boolean);
    return [...defaults, ...configured].some((suffix) =>
      url.hostname === suffix || url.hostname.endsWith(`.${suffix}`));
  } catch (_) {
    return false;
  }
}
