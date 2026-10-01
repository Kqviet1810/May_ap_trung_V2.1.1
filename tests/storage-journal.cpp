#include <cassert>
#include <cstdio>
#include <vector>
#include <algorithm>
#include "../MAYAP_INDUSTRIAL_v4_0_0/storage_journal.h"
struct Payload { uint32_t config, elapsed, running; };
struct IO {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(65536, 0xff);
  std::vector<unsigned> writes = std::vector<unsigned>(65536, 0);
  int budget = -1; bool nack = false, corrupt = false;
  bool readBytes(uint16_t a, void *p, size_t n) {
    if (nack || n > bytes.size() - a) return false;
    memcpy(p, bytes.data() + a, n); return true;
  }
  bool writeBytes(uint16_t a, const void *p, size_t n) {
    if (nack || n > bytes.size() - a) return false;
    for (size_t i = 0; i < n; ++i) {
      if (!budget) return false;
      if (budget > 0) --budget;
      bytes[a+i] = static_cast<const uint8_t *>(p)[i] ^ (corrupt ? 1 : 0);
      ++writes[a+i];
    }
    return true;
  }
};
using J = MayapStorage::Journal<Payload, IO>;
int main() {
  const MayapStorage::Region region{0x1000, 96, 512};
  IO initial; J first(initial, region); assert(first.scan());
  assert(first.append({37, 300, 1}, 1));
  // Cut after EVERY byte, including invalidation and publication marker.
  for (unsigned cut = 0; cut <= sizeof(J::Record) + 2; ++cut) {
    IO io = initial; J writer(io, region); assert(writer.scan()); io.budget = cut;
    writer.append({38, 600, 1}, 2); io.budget = -1;
    J reboot(io, region); assert(reboot.scan() && reboot.found);
    assert(reboot.latest.generation == 1 || reboot.latest.generation == 2);
    assert(reboot.latest.payload.config == (reboot.latest.generation == 1 ? 37U : 38U));
    assert(reboot.latest.payload.elapsed == (reboot.latest.generation == 1 ? 300U : 600U));
  }
  IO io = initial; J journal(io, region); assert(journal.scan());
  for (uint64_t i = 2; i <= 1000; ++i) assert(journal.append({37, uint32_t(i), 1}, i));
  J reboot(io, region); assert(reboot.scan() && reboot.latest.generation == 1000);
  for (unsigned cut = 0; cut <= sizeof(J::Record) + 2; ++cut) {
    IO reused = io; J writer(reused, region); assert(writer.scan()); reused.budget = cut;
    writer.append({38, 1001, 0}, 1001); reused.budget = -1;
    J restored(reused, region); assert(restored.scan());
    assert(restored.latest.generation == 1000 || restored.latest.generation == 1001);
  }
  for (unsigned i = 0; i < 4096; ++i) assert(io.writes[i] == 0); // no index hotspot
  for (unsigned i = 0xd000; i < 65536; ++i) assert(io.writes[i] == 0); // reserve
  io.corrupt = true; assert(!journal.append({1, 2, 0}, 1001)); io.corrupt = false;
  J verified(io, region); assert(verified.scan() && verified.latest.generation == 1000);
  io.nack = true; J unreadable(io, region); assert(!unreadable.scan());
  io.nack = false; assert(!unreadable.append({1,2,3}, 2000)); // partial scan cannot write
  MayapStorage::Health h;
  assert(!h.failed(0, false)); assert(!h.failed(5000, false)); assert(h.failed(10000, false));
  h.good(); assert(!h.failed(15000, false)); h.good(); assert(!h.failed(20000, false));
  for (unsigned t = 0; t < 100000; t += 5000) assert(!h.failed(t, true));
  assert(!h.recovered()); assert(!h.recovered()); assert(!h.recovered()); assert(h.recovered());
  puts("storage journal: all byte power cuts, CRC, wrap, wear, bounds, NACK and health PASS");
}
