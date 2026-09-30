"""Compile the actual I2C supervisor and UART class against fault-injection HALs."""
import argparse
import subprocess
import tempfile
import re
from pathlib import Path

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default='g++')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='mayap-runtime-') as temporary:
    out = Path(temporary)
    i2c = (root / 'MAYAP_INDUSTRIAL_v4_0_0/i2c_supervisor.h').read_text(encoding='utf-8')
    i2c = i2c.replace('#include "config.h"', '').replace('#include <Wire.h>', '')
    (out / 'actual-i2c.inc').write_text(i2c, encoding='utf-8')
    machine = (root / 'MAYAP_INDUSTRIAL_v4_0_0/machine_control.h').read_text(encoding='utf-8')
    start = machine.index('namespace SHT485Config {')
    end = machine.index('\n// ===', machine.index('class SHT485Industrial {', start))
    (out / 'actual-uart.inc').write_text(machine[start:end], encoding='utf-8')
    start = machine.index('  void updateAttinyLink(uint32_t now) {')
    end = machine.index('  void updateBatchTime(uint32_t now) {', start)
    (out / 'actual-attiny-controller.inc').write_text(machine[start:end], encoding='utf-8')
    services = (root / 'MAYAP_INDUSTRIAL_v4_0_0/service_recovery.h').read_text(encoding='utf-8')
    services = services.replace('#include "config.h"', '').replace('#include "runtime_recovery_policy.h"', '')
    (out / 'actual-services.inc').write_text(services, encoding='utf-8')
    network = (root / 'MAYAP_INDUSTRIAL_v4_0_0/network_service.h').read_text(encoding='utf-8')
    start = network.index('inline void mayapRequestWifiDeepRecovery()')
    end = network.index('inline void mayapSetWifiPortalOtaQuiesced', start)
    (out / 'actual-network.inc').write_text(network[start:end], encoding='utf-8')
    realtime = (root / 'MAYAP_INDUSTRIAL_v4_0_0/realtime_link.h').read_text(encoding='utf-8')
    start = realtime.index('inline bool subscribeAll() {')
    end = realtime.index('inline void attemptConnect', start)
    (out / 'actual-mqtt-subscriptions.inc').write_text(realtime[start:end], encoding='utf-8')
    ota = (root / 'MAYAP_INDUSTRIAL_v4_0_0/ota_update.h').read_text(encoding='utf-8')
    ota = '\n'.join(line for line in ota.splitlines() if not line.startswith('#include'))
    (out / 'actual-ota.inc').write_text(ota, encoding='utf-8')
    for name in ('attiny_bus', 'gpio_interrupts'):
        source = (root / ('MAYAP_INDUSTRIAL_v4_0_0/' + name + '.h')).read_text(encoding='utf-8')
        source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
        (out / ('actual-' + name + '.inc')).write_text(source, encoding='utf-8')
    cfg = (root / 'MAYAP_INDUSTRIAL_v4_0_0/config.h').read_text(encoding='utf-8')
    names = ('PIN_ATTINY_BUS', 'ATTINY_COMMAND_WIDTH_MS', 'ATTINY_BUS_MAX_RETRY',
             'ATTINY_MSG_MAX_COMMAND', 'ATTINY_MSG_STATUS_BASE', 'ATTINY_MSG_STATUS_MAX',
             'ATTINY_MSG_BATCH_START', 'ATTINY_MSG_BATCH_END', 'ATTINY_MSG_SIREN_ON',
             'ATTINY_MSG_SIREN_OFF', 'ATTINY_MSG_STATUS_QUERY', 'ATTINY_MSG_ACTIVITY_ON',
             'ATTINY_MSG_ACTIVITY_OFF', 'ATTINY_PROTOCOL_VERSION',
             'ATTINY_STATUS_FLAG_BATCH', 'ATTINY_STATUS_FLAG_9V_LOW',
             'ATTINY_STATUS_FLAG_SIREN', 'ATTINY_STATUS_FLAG_ACTIVITY',
             'ATTINY_ACTIVITY_OFF_CONFIRM_MS', 'ATTINY_STATUS_RESPONSE_TIMEOUT_MS',
             'ATTINY_9V_CONFIRM_MS', 'ATTINY_SIREN_REASSERT_MS', 'ATTINY_RESYNC_RETRY_MS',
             'ATTINY_STATUS_ARMED_INTERVAL_MS', 'ATTINY_STATUS_IDLE_INTERVAL_MS')
    declarations = [re.search(r'constexpr [^;]*\b' + name + r'\b[^;]*;', cfg)[0] for name in names]
    (out / 'actual-attiny-config.inc').write_text('\n'.join(declarations), encoding='utf-8')
    tiny = (root / 'ATTINY13A_POWER_ALARM/ATTINY13A_POWER_ALARM.ino').read_text(encoding='utf-8')
    decoder = re.search(r'static uint8_t decode\(uint16_t w\) \{[^}]*\}', tiny)[0]
    (out / 'actual-tiny-decoder.inc').write_text(decoder, encoding='utf-8')
    boot = (root / 'MAYAP_INDUSTRIAL_v4_0_0/boot_diagnostic.h').read_text(encoding='utf-8')
    mailbox = 'namespace MayapBootInternal { static volatile uint8_t homeReleased=0, operationsReady=0; }\n'
    for name in ('mayapBootHomeReleased', 'mayapBootReleaseHome', 'mayapBootOperationsReady',
                 'mayapBootAcknowledgeHomeFrame'):
        mailbox += re.search(r'inline (?:bool|void) ' + name + r'\(\) \{[^}]*\}', boot)[0] + '\n'
    (out / 'actual-boot-mailbox.inc').write_text(mailbox, encoding='utf-8')
    for test in ('runtime-buses', 'runtime-network', 'runtime-ota', 'runtime-attiny', 'runtime-attiny-state', 'runtime-mqtt-subscriptions'):
        executable = out / (test + ('.exe' if __import__('os').name == 'nt' else ''))
        command = [args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-I', str(out),
                   str(root / ('tests/' + test + '.cpp')), '-o', str(executable)]
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)
