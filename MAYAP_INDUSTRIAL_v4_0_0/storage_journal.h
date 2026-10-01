#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Portable journal used by the firmware and byte-level power-cut tests.
namespace MayapStorage {
struct Geometry { uint32_t capacity; uint16_t page; uint8_t address; };
struct Region { uint16_t base, slots, stride; };
inline uint32_t crc32(const void *data, size_t count) {
  const uint8_t *p = static_cast<const uint8_t *>(data);
  uint32_t crc = 0xffffffffU;
  while (count--) {
    crc ^= *p++;
    for (uint8_t i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}
template<class Payload, class IO> class Journal {
 public:
  struct __attribute__((packed)) Record {
    uint32_t magic;
    uint16_t schema, size;
    uint64_t generation;
    Payload payload;
    uint32_t crc;
  };
  Journal(IO &io, Region region) : io_(io), region_(region) {}
  bool scan() {
    found = false;
    // An unreadable slot is NOT an empty slot. Never write after a partial scan.
    for (uint16_t i = 0; i < region_.slots; ++i) {
      Record r{}; uint8_t commit = 0;
      if (!io_.readBytes(address(i), &r, sizeof(r)) ||
          !io_.readBytes(marker(i), &commit, 1)) return scanned_ = false;
      if (commit == 0xa5 && valid(r) && (!found || r.generation > latest.generation)) {
        latest = r; slot_ = i; found = true;
      }
    }
    return scanned_ = true;
  }
  bool append(const Payload &payload, uint64_t generation) {
    if (!scanned_ || sizeof(Record) >= region_.stride) return false;
    const uint16_t next = found ? (slot_ + 1U) % region_.slots : 0U;
    Record r{}; r.magic = 0x4d41594aU; r.schema = 1; r.size = sizeof(r);
    r.generation = generation; r.payload = payload;
    r.crc = crc32(&r, offsetof(Record, crc));
    uint8_t commit = 0, check = 0xff;
    // Invalidate first, verify body, publish LAST. Old slot is never touched.
    if (!io_.writeBytes(marker(next), &commit, 1) ||
        !io_.readBytes(marker(next), &check, 1) || check != 0 ||
        !io_.writeBytes(address(next), &r, sizeof(r))) return false;
    Record verify{};
    if (!io_.readBytes(address(next), &verify, sizeof(verify)) ||
        memcmp(&r, &verify, sizeof(r)) != 0 || !valid(verify)) return false;
    commit = 0xa5;
    if (!io_.writeBytes(marker(next), &commit, 1) ||
        !io_.readBytes(marker(next), &check, 1) || check != commit) return false;
    latest = r; slot_ = next; found = true;
    return true;
  }
  bool verifyLatest() {
    if (!scanned_) return false;
    // Even an empty chip must successfully read, not merely ACK its address.
    Record r{}; uint8_t commit = 0;
    if (!io_.readBytes(address(slot_), &r, sizeof(r))) return false;
    if (!found) return true;
    return io_.readBytes(marker(slot_), &commit, 1) && commit == 0xa5 &&
        valid(r) && memcmp(&r, &latest, sizeof(r)) == 0;
  }
  static bool valid(const Record &r) {
    return r.magic == 0x4d41594aU && r.schema == 1 && r.size == sizeof(r) &&
           r.crc == crc32(&r, offsetof(Record, crc));
  }
  bool found = false;
  Record latest{};
 private:
  uint16_t address(uint16_t slot) const { return region_.base + slot * region_.stride; }
  uint16_t marker(uint16_t slot) const { return address(slot) + region_.stride - 1U; }
  IO &io_; Region region_; uint16_t slot_ = 0; bool scanned_ = false;
};

// Evidence is counted once per spaced service attempt, never per I2C retry.
class Health {
 public:
  bool failed(uint32_t now, bool busFault) {
    stable = 0;
    if (busFault) { failures = 0; first = now; return false; }
    if (!failures) first = now;
    if (failures < 255) ++failures;
    return failures >= 3 && uint32_t(now - first) >= 10000U;
  }
  void good() { failures = 0; }
  bool recovered() { if (stable < 255) ++stable; return stable >= 4; }
  uint8_t failures = 0, stable = 0;
  uint32_t first = 0;
};
} // namespace MayapStorage
