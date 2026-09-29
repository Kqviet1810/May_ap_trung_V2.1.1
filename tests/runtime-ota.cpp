#include <cassert>
#include <cstdint>
#include <cstdio>
#include "../MAYAP_INDUSTRIAL_v4_0_0/runtime_recovery_policy.h"
static uint32_t clockMs=100;
uint32_t millis() { return clockMs; }
void mayapSerialPrintf(bool, const char *, ...) {}
#include "actual-services.inc"
enum class ConnectivityMode { Offline, Online };
struct NetworkStatus { ConnectivityMode requestedMode=ConnectivityMode::Online; bool connected=true; } network;
NetworkStatus mayapGetNetworkStatus() { return network; }
static bool safe=true, maintenance=false;
bool mayapFirmwareMaintenanceReady() { return safe; }
bool mayapFirmwareMaintenanceActive() { return maintenance; }
void mayapSetFirmwareMaintenanceActive(bool active) { maintenance=active; }
void mayapMarkIntentionalRestart() {}
namespace MayapBoot { enum class RestartReason { ArduinoOta }; }
void mayapBootPlanRestart(MayapBoot::RestartReason, const char *) {}
constexpr const char OTA_PASSWORD[]="test-only";
constexpr const char NETWORK_WIFI_HOSTNAME[]="mayap-test";
constexpr int U_FLASH=0;
enum ota_error_t { OTA_AUTH_ERROR, OTA_BEGIN_ERROR, OTA_CONNECT_ERROR, OTA_RECEIVE_ERROR, OTA_END_ERROR };
struct FakeUpdate { unsigned aborts=0; void abort() { ++aborts; } } Update;
struct FakeOta {
  unsigned begins=0, ends=0, handles=0;
  int getCommand() { return U_FLASH; }
  void end() { ++ends; }
  void begin() { ++begins; }
  void handle() { ++handles; }
  void setHostname(const char *) {}
  void setPassword(const char *) {}
  void onStart(void (*)()) {}
  void onEnd(void (*)()) {}
  void onProgress(void (*)(unsigned,unsigned)) {}
  void onError(void (*)(ota_error_t)) {}
} ArduinoOTA;
#include "actual-ota.inc"
int main() {
  mayapServiceAdmit(MayapRecovery::Service::Ota);
  mayapOtaBegin(); mayapOtaUpdate(clockMs);
  assert(ArduinoOTA.begins==1 && MayapOtaInternal::started);
  MayapOtaInternal::onStart();
  assert(maintenance && mayapOtaInProgress());
  clockMs+=50000;
  assert(!mayapOtaRuntimeRecover(clockMs) && Update.aborts==0);
  assert(!mayapOtaQuiesceForWifiPortal());
  MayapOtaInternal::onProgress(100,200);
  clockMs+=50000;
  assert(!mayapOtaRuntimeRecover(clockMs)); // Actual progress refreshes deadline.
  clockMs+=10001;
  assert(mayapOtaRuntimeRecover(clockMs));
  assert(Update.aborts==1 && !maintenance && !mayapOtaInProgress() && !MayapOtaInternal::started);
  mayapOtaUpdate(clockMs);
  assert(ArduinoOTA.begins==2);
  safe=false;
  MayapOtaInternal::onStart();
  assert(Update.aborts==2 && !maintenance && !mayapOtaInProgress());
  mayapOtaUpdate(clockMs);
  assert(!MayapOtaInternal::started);
  safe=true; mayapOtaUpdate(clockMs);
  MayapOtaInternal::onStart(); MayapOtaInternal::onError(OTA_RECEIVE_ERROR);
  assert(!mayapOtaInProgress() && !maintenance);
  assert(mayapOtaQuiesceForWifiPortal() && !MayapOtaInternal::started);
  std::puts("Actual OTA: progress protects upload, idle-stuck session abort/reinit, maintenance safety and error cleanup PASS");
}
