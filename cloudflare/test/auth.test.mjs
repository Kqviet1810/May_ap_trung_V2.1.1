import assert from 'node:assert/strict';
import test from 'node:test';

import {
  hashDeviceKey,
  isValidDeviceId,
  isValidFactoryPin,
  isTrustedPushEndpoint,
  timingSafeEqual,
  verifyDeviceKey,
} from '../src/auth.js';

const PEPPER = 'pepper-thu-nghiem-co-it-nhat-32-ky-tu';

test('chi chap nhan Device ID dung dinh dang xuat xuong', () => {
  assert.equal(isValidDeviceId('MAP-441BF6E051D0'), true);
  assert.equal(isValidDeviceId('map-441bf6e051d0'), false);
  assert.equal(isValidDeviceId('MAP-441BF6E051D'), false);
  assert.equal(isValidDeviceId('../MAP-441BF6E051D0'), false);
});

test('factory PIN bat buoc dung 6 chu so', () => {
  assert.equal(isValidFactoryPin('284105'), true);
  assert.equal(isValidFactoryPin('1111'), false);
  assert.equal(isValidFactoryPin('12345a'), false);
});

test('hash/verify fail-closed khi thieu pepper', async () => {
  const hash = await hashDeviceKey('device-secret-32-ky-tu-tro-len-123', PEPPER);
  assert.equal(hash.length, 64);
  assert.equal(await verifyDeviceKey('device-secret-32-ky-tu-tro-len-123', PEPPER, hash), true);
  assert.equal(await verifyDeviceKey('sai-secret', PEPPER, hash), false);
  await assert.rejects(() => hashDeviceKey('secret', 'pepper-ngan'));
});

test('so sanh constant-time cho chuoi cung do dai', () => {
  assert.equal(timingSafeEqual('abcd', 'abcd'), true);
  assert.equal(timingSafeEqual('abcd', 'abce'), false);
  assert.equal(timingSafeEqual('abcd', 'abc'), false);
});

test('chi cho phep Push endpoint HTTPS tu nha cung cap da review', () => {
  assert.equal(isTrustedPushEndpoint('https://fcm.googleapis.com/fcm/send/abc'), true);
  assert.equal(isTrustedPushEndpoint('https://web.push.apple.com/Qabc'), true);
  assert.equal(isTrustedPushEndpoint('http://fcm.googleapis.com/fcm/send/abc'), false);
  assert.equal(isTrustedPushEndpoint('https://googleapis.com.evil.example/x'), false);
  assert.equal(isTrustedPushEndpoint('https://push.example.vn/x', {
    PUSH_ENDPOINT_HOST_SUFFIXES: 'push.example.vn',
  }), true);
});
