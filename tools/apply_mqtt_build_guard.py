from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8")


# 1) Firmware: fail compile if a deployable MQTT configuration is incomplete.
config_path = "MAYAP_INDUSTRIAL_v3_4_0/config.h"
config = read(config_path)
old_comment = """// Broker mac dinh la broker cong cong (chi de kiem tra, xem canh bao trong
// config.js ban web). May thuong mai PHAI doi sang broker rieng + tai khoan
// bang cach dinh nghia lai cac macro nay truoc khi include config.h (vi du
// qua build_flags), khong sua truc tiep gia tri mac dinh o day.
//
// F-01 (audit truoc phat hanh v3.7.1): khi MQTT_USERNAME/MQTT_PASSWORD con la
// chuoi rong (gia tri mac dinh o duoi day), realtime_link.h::mqttCommandChannelTrusted()
// tra ve false va KHOA toan bo lenh dieu khien/cau hinh tu xa qua MQTT (chi
// con publish mot chieu snapshot/presence/log) - vi broker cong khai khong
// xac thuc nghia la BAT KY AI tren internet co the gui lenh that (dung me,
// tat coi khan cap, doi cau hinh...) neu khong co hang rao nay. Dat 2 macro
// nay (tro toi broker rieng) la cach duy nhat de bat lai dieu khien tu xa.
"""
new_comment = """// Host/port/CA cong khai nam trong build_public.h. Username/password MQTT la
// BUILD SECRET: local build phai tao build_secrets.h; CI branch/tag tu tao file
// nay tu GitHub Secrets. Tuyet doi khong commit credential that vao repo.
//
// Fail-fast la chu dich: firmware co realtime Web nen mot binary deploy ma
// username/password rong la binary loi. Truoc day code im lang fallback thanh
// chuoi rong, van compile/boot va broker chi tra state=5 UNAUTHORIZED.
// Cac static_assert ben duoi chan loi ngay luc compile de khong lap lai su co.
"""
if config.count(old_comment) != 1:
    raise SystemExit(f"PATCH FAIL mqtt comment: expected 1, found {config.count(old_comment)}")
config = config.replace(old_comment, new_comment, 1)
old_constants = """constexpr char MQTT_BROKER_HOST[] = MAYAP_MQTT_HOST;
constexpr uint16_t MQTT_BROKER_PORT = MAYAP_MQTT_PORT;
constexpr bool MQTT_USE_TLS = (MAYAP_MQTT_USE_TLS) != 0;
constexpr char MQTT_USERNAME[] = MAYAP_MQTT_USERNAME;
constexpr char MQTT_PASSWORD[] = MAYAP_MQTT_PASSWORD;
constexpr char MQTT_TOPIC_ROOT[] = MAYAP_MQTT_TOPIC_ROOT;
"""
new_constants = old_constants + """
// Deploy invariant: khong cho tao .bin neu realtime MQTT khong co host/account.
static_assert(sizeof(MQTT_BROKER_HOST) > 1U,
              "THIEU MAYAP_MQTT_HOST trong build_public.h");
static_assert(MQTT_BROKER_PORT != 0U,
              "MAYAP_MQTT_PORT khong hop le");
static_assert(sizeof(MQTT_USERNAME) > 1U,
              "THIEU MAYAP_MQTT_USERNAME: tao build_secrets.h tu build_secrets.example.h");
static_assert(sizeof(MQTT_PASSWORD) > 1U,
              "THIEU MAYAP_MQTT_PASSWORD: tao build_secrets.h tu build_secrets.example.h");
"""
if config.count(old_constants) != 1:
    raise SystemExit(f"PATCH FAIL mqtt constants: expected 1, found {config.count(old_constants)}")
config = config.replace(old_constants, new_constants, 1)
write(config_path, config)
print("PATCH OK config MQTT fail-fast")

# 2) Local-build template: make correct usage explicit.
write("MAYAP_INDUSTRIAL_v3_4_0/build_secrets.example.h", """#pragma once

// LOCAL BUILD ONLY:
// 1) Copy file nay thanh build_secrets.h trong CUNG thu muc sketch.
// 2) Dien tai khoan HiveMQ that.
// 3) build_secrets.h da nam trong .gitignore - KHONG commit credential that.
//
// Neu bo qua buoc nay, firmware se FAIL COMPILE co chu dich thay vi tao .bin
// co user/pass rong roi lap MQTT state=5 (UNAUTHORIZED) tren may that.
#define MAYAP_MQTT_USERNAME ""
#define MAYAP_MQTT_PASSWORD ""
""")
print("PATCH OK build_secrets.example.h")

# 3) Regression: source guard may never disappear silently.
checker_path = "tools/check_v381_reliability.py"
checker = read(checker_path)
anchor = 'require_re(config, r\'MAYAP_FIRMWARE_VERSION\\[\\]\\s*=\\s*"3\\.8\\.1"\', "firmware version")\n'
checks = anchor + """

# MQTT deploy image must fail at compile time if broker credentials are absent.
require(config, "static_assert(sizeof(MQTT_BROKER_HOST) > 1U", "MQTT host compile guard")
require(config, "static_assert(sizeof(MQTT_USERNAME) > 1U", "MQTT username compile guard")
require(config, "static_assert(sizeof(MQTT_PASSWORD) > 1U", "MQTT password compile guard")
"""
if checker.count(anchor) != 1:
    raise SystemExit(f"PATCH FAIL checker anchor: expected 1, found {checker.count(anchor)}")
checker = checker.replace(anchor, checks, 1)
write(checker_path, checker)
print("PATCH OK reliability checker")

# 4) README: document the only valid local-build path.
readme_path = "README.md"
readme = read(readme_path)
marker = "### Build profile\n"
section = """### MQTT credential bắt buộc khi build local

Firmware ESP32 **không còn cho phép tạo `.bin` với MQTT username/password rỗng**.
Khi build bằng Arduino IDE/CLI trên máy cá nhân:

1. copy `MAYAP_INDUSTRIAL_v3_4_0/build_secrets.example.h` thành
   `MAYAP_INDUSTRIAL_v3_4_0/build_secrets.h`;
2. điền `MAYAP_MQTT_USERNAME` và `MAYAP_MQTT_PASSWORD` thật của HiveMQ;
3. compile lại firmware. `build_secrets.h` đã nằm trong `.gitignore` và không được commit.

Nếu file thiếu hoặc credential rỗng, compile phải fail. Đây là invariant có chủ ý để
không thể phát sinh lại binary vẫn boot nhưng MQTT lặp `state=5 (UNAUTHORIZED)`.
GitHub Actions branch/tag tự tạo `build_secrets.h` từ Repository Secrets; PR chỉ dùng
placeholder không bí mật để kiểm compile và không phát hành binary deploy.

"""
if section not in readme:
    if readme.count(marker) != 1:
        raise SystemExit(f"PATCH FAIL README marker: expected 1, found {readme.count(marker)}")
    readme = readme.replace(marker, section + marker, 1)
write(readme_path, readme)
print("PATCH OK README")

print("ALL SOURCE MQTT BUILD-GUARD PATCHES APPLIED")
