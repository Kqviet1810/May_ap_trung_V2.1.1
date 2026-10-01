#pragma once

#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <stdint.h>

// I2C mutex duoc tao/define trong file .ino truoc khi include header nay.
bool mayapI2cLock(uint32_t timeoutMs);
void mayapI2cUnlock();

struct MayapTemperatureHistoryPoint {
  uint32_t epoch = 0U;
  int16_t temperatureX10 = 0;
};

namespace MayapTemperatureHistoryInternal {

// Record 4 byte:
//   byte0: bucket[7:0]
//   byte1: bucket[11:8] | tempCode[3:0] << 4
//   byte2: tempCode[11:4]
//   byte3: CRC-8 (poly 0x07) cua 3 byte dau
// bucket = floor(epoch/300) mod 4096; tempCode = round(temp*10)+500.
// Slot vat ly = floor(epoch/300) mod 288. Khong metadata/index ghi dinh ky,
// nen moi slot chi bi viet lai xap xi 1 lan/ngay; moi page 32B nhan 8 lan/ngay.
static uint32_t lastSampleBucket = UINT32_MAX;
static volatile uint32_t latestRtcEpoch = 0U;
static uint32_t cachedRaw[TEMP_HISTORY_SLOT_COUNT]{};
static uint8_t cachedValid[TEMP_HISTORY_SLOT_COUNT]{};

inline uint8_t crc8(const uint8_t *data, size_t length) {
  uint8_t crc = 0U;
  while (length--) {
    crc ^= *data++;
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      crc = (crc & 0x80U) ? static_cast<uint8_t>((crc << 1U) ^ 0x07U)
                          : static_cast<uint8_t>(crc << 1U);
    }
  }
  return crc;
}

inline uint16_t addressForBucket(uint32_t absoluteBucket) {
  const uint16_t slot = static_cast<uint16_t>(absoluteBucket % TEMP_HISTORY_SLOT_COUNT);
  return static_cast<uint16_t>(EEPROM_ADDR_TEMP_HISTORY +
                               slot * TEMP_HISTORY_RECORD_BYTES);
}

inline bool readRaw(uint16_t address, uint8_t out[TEMP_HISTORY_RECORD_BYTES]) {
  if (!mayapStoragePrimaryOnline()) return false;
  if (!mayapI2cLock(I2C_STORAGE_LOCK_TIMEOUT_MS)) return false;
  Wire.beginTransmission(EEPROM_PRIMARY_ADDRESS);
  Wire.write(static_cast<uint8_t>(address >> 8U));
  Wire.write(static_cast<uint8_t>(address & 0xFFU));
  if (Wire.endTransmission(false) != 0U) {
    mayapI2cUnlock();
    return false;
  }
  const size_t got = Wire.requestFrom(EEPROM_PRIMARY_ADDRESS,
                                      static_cast<uint8_t>(TEMP_HISTORY_RECORD_BYTES),
                                      static_cast<uint8_t>(true));
  bool ok = got == TEMP_HISTORY_RECORD_BYTES;
  for (uint8_t i = 0U; i < TEMP_HISTORY_RECORD_BYTES && ok; ++i) {
    if (!Wire.available()) { ok = false; break; }
    out[i] = static_cast<uint8_t>(Wire.read());
  }
  mayapI2cUnlock();
  if (ok && address >= EEPROM_ADDR_TEMP_HISTORY &&
      address < EEPROM_ADDR_TEMP_HISTORY + TEMP_HISTORY_STORAGE_BYTES) {
    const uint16_t slot = (address - EEPROM_ADDR_TEMP_HISTORY) / TEMP_HISTORY_RECORD_BYTES;
    uint32_t raw = 0; memcpy(&raw, out, sizeof(raw));
    __atomic_store_n(&cachedRaw[slot], raw, __ATOMIC_RELEASE);
    __atomic_store_n(&cachedValid[slot], 1U, __ATOMIC_RELEASE);
  }
  return ok;
}

inline bool writeRaw(uint16_t address, const uint8_t data[TEMP_HISTORY_RECORD_BYTES]) {
  // Dia chi history luon boi so 4 va record 4B, nen khong bao gio vuot page 32B.
  if ((address % EEPROM_PRIMARY_PAGE) > EEPROM_PRIMARY_PAGE - TEMP_HISTORY_RECORD_BYTES) return false;
  if (!mayapStoragePrimaryOnline()) return false;
  if (!mayapI2cLock(I2C_STORAGE_LOCK_TIMEOUT_MS)) return false;
  Wire.beginTransmission(EEPROM_PRIMARY_ADDRESS);
  Wire.write(static_cast<uint8_t>(address >> 8U));
  Wire.write(static_cast<uint8_t>(address & 0xFFU));
  const size_t written = Wire.write(data, TEMP_HISTORY_RECORD_BYTES);
  const uint8_t err = Wire.endTransmission(true);
  if (written != TEMP_HISTORY_RECORD_BYTES || err != 0U) {
    mayapI2cUnlock();
    return false;
  }

  // ACK polling: AT24C32 tu ghi noi bo toi da ~5ms; gioi han 20ms giong driver
  // storage chinh. Chi mot record 4B/5phut, khong nam tren duong dieu khien nong.
  mayapI2cUnlock();
  const uint32_t started = millis();
  bool ready = false;
  do {
    if (!mayapI2cLock(I2C_STORAGE_LOCK_TIMEOUT_MS)) return false;
    Wire.beginTransmission(EEPROM_PRIMARY_ADDRESS);
    ready = Wire.endTransmission(true) == 0U;
    mayapI2cUnlock();
    if (!ready) delay(1);
  } while (!ready && static_cast<uint32_t>(millis() - started) < EEPROM_WRITE_TIMEOUT_MS);
  if (!ready) return false;
  uint8_t verify[TEMP_HISTORY_RECORD_BYTES]{};
  return readRaw(address, verify) && memcmp(data, verify, TEMP_HISTORY_RECORD_BYTES) == 0;
}

inline bool encode(uint32_t absoluteBucket, float temperature,
                   uint8_t out[TEMP_HISTORY_RECORD_BYTES]) {
  if (!isfinite(temperature) || temperature < -20.0f || temperature > 100.0f) return false;
  const int32_t temp10 = lroundf(temperature * 10.0f);
  const int32_t codeSigned = temp10 + 500;
  if (codeSigned < 0 || codeSigned > 4095) return false;
  const uint16_t tag = static_cast<uint16_t>(absoluteBucket & 0x0FFFU);
  const uint16_t code = static_cast<uint16_t>(codeSigned);
  out[0] = static_cast<uint8_t>(tag & 0xFFU);
  out[1] = static_cast<uint8_t>(((tag >> 8U) & 0x0FU) | ((code & 0x0FU) << 4U));
  out[2] = static_cast<uint8_t>((code >> 4U) & 0xFFU);
  out[3] = crc8(out, 3U);
  return true;
}

inline bool decode(uint32_t absoluteBucket,
                   const uint8_t raw[TEMP_HISTORY_RECORD_BYTES],
                   MayapTemperatureHistoryPoint &out) {
  if (crc8(raw, 3U) != raw[3]) return false;
  const uint16_t tag = static_cast<uint16_t>(raw[0] | ((raw[1] & 0x0FU) << 8U));
  if (tag != static_cast<uint16_t>(absoluteBucket & 0x0FFFU)) return false;
  const uint16_t code = static_cast<uint16_t>(((raw[1] >> 4U) & 0x0FU) |
                                              (static_cast<uint16_t>(raw[2]) << 4U));
  const int16_t temp10 = static_cast<int16_t>(static_cast<int32_t>(code) - 500);
  if (temp10 < -200 || temp10 > 1000) return false;
  out.epoch = absoluteBucket * TEMP_HISTORY_SAMPLE_SEC;
  out.temperatureX10 = temp10;
  return true;
}

inline bool readBucket(uint32_t absoluteBucket, MayapTemperatureHistoryPoint &out) {
  uint8_t raw[TEMP_HISTORY_RECORD_BYTES]{};
  if (!readRaw(addressForBucket(absoluteBucket), raw)) return false;
  return decode(absoluteBucket, raw, out);
}

inline bool writeBucket(uint32_t absoluteBucket, float temperature) {
  uint8_t raw[TEMP_HISTORY_RECORD_BYTES]{};
  if (!encode(absoluteBucket, temperature, raw)) return false;
  return writeRaw(addressForBucket(absoluteBucket), raw);
}

}  // namespace MayapTemperatureHistoryInternal

inline void mayapTemperatureHistoryWriteSample(uint32_t rtcEpoch, float temperature,
                                          bool sensorUsable) {
  using namespace MayapTemperatureHistoryInternal;
  if (rtcEpoch == 0U) return;
  __atomic_store_n(&latestRtcEpoch, rtcEpoch, __ATOMIC_RELEASE);
  if (!sensorUsable || !isfinite(temperature)) return;

  const uint32_t bucket = rtcEpoch / TEMP_HISTORY_SAMPLE_SEC;
  if (bucket == lastSampleBucket) return;

  // Sau reboot co the slot hien tai da duoc ghi truoc do. Doc kiem tra truoc
  // de khong ghi lai cung mot page vo ich. Neu doc fail/record cu thi ghi mau moi.
  MayapTemperatureHistoryPoint existing{};
  const bool alreadyStored = readBucket(bucket, existing);
  lastSampleBucket = bucket;  // history la best-effort; loi I2C khong duoc hammer moi 5ms.
  if (alreadyStored) return;
  (void)writeBucket(bucket, temperature);
}

inline uint32_t mayapTemperatureHistoryLatestEpoch() {
  return __atomic_load_n(&MayapTemperatureHistoryInternal::latestRtcEpoch,
                         __ATOMIC_ACQUIRE);
}

// Network/MQTT never touches I2C. The worker gradually fills the RAM cache.
inline uint8_t mayapTemperatureHistoryReadStatus(uint32_t bucket,
                                                  MayapTemperatureHistoryPoint &out) {
  using namespace MayapTemperatureHistoryInternal;
  const uint16_t slot = bucket % TEMP_HISTORY_SLOT_COUNT;
  if (!mayapStoragePrimaryOnline() || !__atomic_load_n(&cachedValid[slot], __ATOMIC_ACQUIRE)) return 0;
  const uint32_t value = __atomic_load_n(&cachedRaw[slot], __ATOMIC_ACQUIRE);
  uint8_t raw[TEMP_HISTORY_RECORD_BYTES]; memcpy(raw, &value, sizeof(raw));
  return decode(bucket, raw, out) ? 2U : 1U;
}
inline bool mayapTemperatureHistoryReadBucket(uint32_t bucket, MayapTemperatureHistoryPoint &out) {
  return mayapTemperatureHistoryReadStatus(bucket, out) == 2U;
}

// Control publishes a bounded RAM mailbox; worker owns all history writes.
namespace MayapHistoryMailbox {
static uint8_t pending = 0;
static uint32_t epoch = 0;
static float temperature = 0;
}
inline void mayapTemperatureHistorySample(uint32_t epoch, float temperature, bool usable) {
  if (epoch) __atomic_store_n(&MayapTemperatureHistoryInternal::latestRtcEpoch, epoch, __ATOMIC_RELEASE);
  if (!usable || !mayapStoragePrimaryOnline() || !isfinite(temperature)) return;
  if (__atomic_load_n(&MayapHistoryMailbox::pending, __ATOMIC_ACQUIRE)) return;
  MayapHistoryMailbox::epoch = epoch;
  MayapHistoryMailbox::temperature = temperature;
  __atomic_store_n(&MayapHistoryMailbox::pending, 1U, __ATOMIC_RELEASE);
}
inline void mayapTemperatureHistoryService() {
  using namespace MayapTemperatureHistoryInternal;
  static uint16_t scanSlot = 0;
  static bool wasPrimary = false;
  if (!mayapStoragePrimaryOnline()) {
    wasPrimary = false;
    __atomic_store_n(&MayapHistoryMailbox::pending, 0U, __ATOMIC_RELEASE);
    return;
  }
  if (!wasPrimary) {
    scanSlot = 0;
    for (uint16_t i = 0; i < TEMP_HISTORY_SLOT_COUNT; ++i)
      __atomic_store_n(&cachedValid[i], 0U, __ATOMIC_RELEASE);
    wasPrimary = true;
  }
  if (scanSlot < TEMP_HISTORY_SLOT_COUNT) {
    uint8_t raw[TEMP_HISTORY_RECORD_BYTES]{};
    (void)readRaw(EEPROM_ADDR_TEMP_HISTORY + scanSlot * TEMP_HISTORY_RECORD_BYTES, raw);
    ++scanSlot;
  }
  if (!__atomic_load_n(&MayapHistoryMailbox::pending, __ATOMIC_ACQUIRE)) return;
  if (mayapStoragePrimaryOnline())
    mayapTemperatureHistoryWriteSample(MayapHistoryMailbox::epoch, MayapHistoryMailbox::temperature, true);
  __atomic_store_n(&MayapHistoryMailbox::pending, 0U, __ATOMIC_RELEASE);
}
