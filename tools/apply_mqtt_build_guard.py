from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(path: str, old: str, new: str, label: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"PATCH FAIL {label}: expected 1 match, found {count}")
    write(path, text.replace(old, new, 1))
    print(f"PATCH OK {label}")


# 1) Firmware must never compile a deployable image with empty broker credentials.
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
// username/password rong la binary loi. Truoc v3.8.1 code im lang fallback
// thanh chuoi rong, van compile/boot va broker chi tra state=5 UNAUTHORIZED.
// Cac static_assert ben duoi chan loi ngay luc compile de khong lap lai su co.
"""
if old_comment not in config:
    raise SystemExit("PATCH FAIL mqtt comment: source drift")
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
// PR CI duoc workflow tao credential gia chi de kiem compile; branch/tag van
// bat buoc lay credential that tu GitHub Secrets.
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

# 2) Make the local secret template explicit and hard to misuse.
example_path = "MAYAP_INDUSTRIAL_v3_4_0/build_secrets.example.h"
write(example_path, """#pragma once

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

# 3) CI: PR uses non-secret placeholders only for compile. Branch/tag must use real Secrets.
workflow_path = ".github/workflows/build-firmware.yml"
workflow = read(workflow_path)
old_secret_step = """      - name: Tao MQTT secret cho build deploy
        if: github.event_name != 'pull_request'
        env:
          MAYAP_MQTT_USERNAME: ${{ secrets.MAYAP_MQTT_USERNAME }}
          MAYAP_MQTT_PASSWORD: ${{ secrets.MAYAP_MQTT_PASSWORD }}
        run: |
          python3 - <<'PY'
          import json, os
          from pathlib import Path

          username = os.getenv(\"MAYAP_MQTT_USERNAME\", \"\")
          password = os.getenv(\"MAYAP_MQTT_PASSWORD\", \"\")
          if not username or not password:
              raise SystemExit(\"Thieu GitHub Secrets: MAYAP_MQTT_USERNAME/MAYAP_MQTT_PASSWORD\")

          Path(\"MAYAP_INDUSTRIAL_v3_4_0/build_secrets.h\").write_text(
              \"#pragma once\\n\"
              + \"#define MAYAP_MQTT_USERNAME \" + json.dumps(username) + \"\\n\"
              + \"#define MAYAP_MQTT_PASSWORD \" + json.dumps(password) + \"\\n\",
              encoding=\"utf-8\",
          )
          PY
"""
new_secret_step = """      - name: Tao MQTT credential cho compile
        env:
          MAYAP_MQTT_USERNAME: ${{ secrets.MAYAP_MQTT_USERNAME }}
          MAYAP_MQTT_PASSWORD: ${{ secrets.MAYAP_MQTT_PASSWORD }}
        run: |
          python3 - <<'PY'
          import json, os
          from pathlib import Path

          if os.getenv(\"GITHUB_EVENT_NAME\") == \"pull_request\":
              # PR khong duoc doc repository secrets. Dung placeholder KHONG BI MAT
              # chi de compile/regression; PR workflow khong phat hanh artifact deploy.
              username = \"__ci_pr_mqtt_user__\"
              password = \"__ci_pr_mqtt_password__\"
          else:
              username = os.getenv(\"MAYAP_MQTT_USERNAME\", \"\").strip()
              password = os.getenv(\"MAYAP_MQTT_PASSWORD\", \"\")
              if not username or not password:
                  raise SystemExit(\"Thieu GitHub Secrets: MAYAP_MQTT_USERNAME/MAYAP_MQTT_PASSWORD\")

          Path(\"MAYAP_INDUSTRIAL_v3_4_0/build_secrets.h\").write_text(
              \"#pragma once\\n\"
              + \"#define MAYAP_MQTT_USERNAME \" + json.dumps(username) + \"\\n\"
              + \"#define MAYAP_MQTT_PASSWORD \" + json.dumps(password) + \"\\n\",
              encoding=\"utf-8\",
          )
          PY
"""
if workflow.count(old_secret_step) != 1:
    raise SystemExit(f"PATCH FAIL workflow credential step: expected 1, found {workflow.count(old_secret_step)}")
workflow = workflow.replace(old_secret_step, new_secret_step, 1)
old_artifact = """      - name: Luu firmware pilot
        if: github.event_name == 'workflow_dispatch'
        uses: actions/upload-artifact@v6
        with:
          name: firmware-pilot
          path: |
            ./build/MAYAP_INDUSTRIAL_v3_4_0.ino.bin
            ./build-attiny/ATTINY13A_POWER_ALARM.hex
          retention-days: 7
"""
new_artifact = """      - name: Luu firmware test
        if: github.event_name == 'workflow_dispatch' || startsWith(github.ref, 'refs/heads/hardening/')
        uses: actions/upload-artifact@v6
        with:
          name: firmware-test-${{ github.sha }}
          path: |
            ./build/MAYAP_INDUSTRIAL_v3_4_0.ino.bin
            ./build-attiny/ATTINY13A_POWER_ALARM.hex
          retention-days: 7
"""
if workflow.count(old_artifact) != 1:
    raise SystemExit(f"PATCH FAIL workflow artifact step: expected 1, found {workflow.count(old_artifact)}")
workflow = workflow.replace(old_artifact, new_artifact, 1)
write(workflow_path, workflow)
print("PATCH OK build workflow")

# 4) Regression tripwires: enforce the guard and CI credential policy forever.
checker_path = "tools/check_v381_reliability.py"
checker = read(checker_path)
old_reads = 'safety = read("doc/SAFETY_HARDWARE_REQUIREMENTS.md")\n'
new_reads = old_reads + 'build_workflow = read(".github/workflows/build-firmware.yml")\n'
if checker.count(old_reads) != 1:
    raise SystemExit("PATCH FAIL checker reads")
checker = checker.replace(old_reads, new_reads, 1)
anchor = 'require_re(config, r\'MAYAP_FIRMWARE_VERSION\\[\\]\\s*=\\s*"3\\.8\\.1"\', "firmware version")\n'
checks = anchor + """

# MQTT deploy image must fail at compile time if broker credentials are absent.
require(config, "static_assert(sizeof(MQTT_BROKER_HOST) > 1U", "MQTT host compile guard")
require(config, "static_assert(sizeof(MQTT_USERNAME) > 1U", "MQTT username compile guard")
require(config, "static_assert(sizeof(MQTT_PASSWORD) > 1U", "MQTT password compile guard")
require(build_workflow, 'username = "__ci_pr_mqtt_user__"', "PR MQTT compile placeholder")
require(build_workflow, 'Thieu GitHub Secrets: MAYAP_MQTT_USERNAME/MAYAP_MQTT_PASSWORD', "deploy MQTT secrets gate")
require(build_workflow, "startsWith(github.ref, 'refs/heads/hardening/')", "hardening test artifact")
"""
if checker.count(anchor) != 1:
    raise SystemExit("PATCH FAIL checker anchor")
checker = checker.replace(anchor, checks, 1)
write(checker_path, checker)
print("PATCH OK reliability checker")

# 5) README: make local-build procedure explicit.
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
        raise SystemExit("PATCH FAIL README build profile marker")
    readme = readme.replace(marker, section + marker, 1)
    write(readme_path, readme)
print("PATCH OK README")

print("ALL MQTT BUILD-GUARD PATCHES APPLIED")
