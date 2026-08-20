// Tat ca truy van D1 tap trung o day - de bao tri, khong rai SQL khap noi.

export async function getDeviceByDeviceId(db, deviceId) {
  return db
    .prepare('SELECT * FROM devices WHERE device_id = ?1')
    .bind(deviceId)
    .first();
}

export async function insertDevice(db, { deviceId, deviceName, deviceKeyHash, pairingToken, now }) {
  await db
    .prepare(
      `INSERT INTO devices (device_id, device_name, device_key_hash, pairing_token, created_at, last_seen, status)
       VALUES (?1, ?2, ?3, ?4, ?5, ?5, 'online')`
    )
    .bind(deviceId, deviceName || '', deviceKeyHash, pairingToken, now)
    .run();
}

export async function touchDevice(db, deviceId, status, now, deviceName) {
  if (deviceName) {
    await db
      .prepare('UPDATE devices SET last_seen = ?2, status = ?3, device_name = ?4 WHERE device_id = ?1')
      .bind(deviceId, now, status, deviceName)
      .run();
  } else {
    await db
      .prepare('UPDATE devices SET last_seen = ?2, status = ?3 WHERE device_id = ?1')
      .bind(deviceId, now, status)
      .run();
  }
}

export async function getSubscriptionsForDevice(db, deviceId) {
  const { results } = await db
    .prepare('SELECT * FROM push_subscriptions WHERE device_id = ?1')
    .bind(deviceId)
    .all();
  return results || [];
}

export async function getSubscriptionByEndpoint(db, endpoint) {
  return db
    .prepare('SELECT * FROM push_subscriptions WHERE endpoint = ?1')
    .bind(endpoint)
    .first();
}

export async function upsertSubscription(db, { deviceId, endpoint, p256dh, auth, userAgent, now }) {
  await db
    .prepare(
      `INSERT INTO push_subscriptions (device_id, endpoint, p256dh, auth, user_agent, created_at, updated_at)
       VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?6)
       ON CONFLICT(endpoint) DO UPDATE SET
         device_id = excluded.device_id,
         p256dh = excluded.p256dh,
         auth = excluded.auth,
         user_agent = excluded.user_agent,
         updated_at = excluded.updated_at`
    )
    .bind(deviceId, endpoint, p256dh, auth, userAgent || '', now)
    .run();
}

export async function deleteSubscriptionByEndpoint(db, endpoint) {
  await db.prepare('DELETE FROM push_subscriptions WHERE endpoint = ?1').bind(endpoint).run();
}

export async function getAlarmState(db, deviceId, alarmType) {
  return db
    .prepare('SELECT * FROM alarm_state WHERE device_id = ?1 AND alarm_type = ?2')
    .bind(deviceId, alarmType)
    .first();
}

export async function upsertAlarmState(db, { deviceId, alarmType, active, firstSentAt, lastSentAt, lastMessage }) {
  await db
    .prepare(
      `INSERT INTO alarm_state (device_id, alarm_type, active, first_sent_at, last_sent_at, last_message)
       VALUES (?1, ?2, ?3, ?4, ?5, ?6)
       ON CONFLICT(device_id, alarm_type) DO UPDATE SET
         active = excluded.active,
         first_sent_at = COALESCE(alarm_state.first_sent_at, excluded.first_sent_at),
         last_sent_at = excluded.last_sent_at,
         last_message = excluded.last_message`
    )
    .bind(deviceId, alarmType, active ? 1 : 0, firstSentAt, lastSentAt, lastMessage || '')
    .run();
}

export async function insertAlarmLog(db, entry) {
  await db
    .prepare(
      `INSERT INTO alarm_log
        (device_id, alarm_type, severity, state, message, temperature, humidity, notification_sent, created_at)
       VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)`
    )
    .bind(
      entry.deviceId,
      entry.alarmType,
      entry.severity,
      entry.state,
      entry.message,
      entry.temperature ?? null,
      entry.humidity ?? null,
      entry.notificationSent ? 1 : 0,
      entry.now
    )
    .run();
}
