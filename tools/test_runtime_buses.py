"""Compile the actual I2C supervisor and UART class against fault-injection HALs."""
import argparse
import subprocess
import tempfile
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
    services = (root / 'MAYAP_INDUSTRIAL_v4_0_0/service_recovery.h').read_text(encoding='utf-8')
    services = services.replace('#include "config.h"', '').replace('#include "runtime_recovery_policy.h"', '')
    (out / 'actual-services.inc').write_text(services, encoding='utf-8')
    network = (root / 'MAYAP_INDUSTRIAL_v4_0_0/network_service.h').read_text(encoding='utf-8')
    start = network.index('inline void mayapRequestWifiDeepRecovery()')
    end = network.index('inline void mayapSetWifiPortalOtaQuiesced', start)
    (out / 'actual-network.inc').write_text(network[start:end], encoding='utf-8')
    ota = (root / 'MAYAP_INDUSTRIAL_v4_0_0/ota_update.h').read_text(encoding='utf-8')
    ota = '\n'.join(line for line in ota.splitlines() if not line.startswith('#include'))
    (out / 'actual-ota.inc').write_text(ota, encoding='utf-8')
    for test in ('runtime-buses', 'runtime-network', 'runtime-ota'):
        executable = out / (test + ('.exe' if __import__('os').name == 'nt' else ''))
        command = [args.cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-I', str(out),
                   str(root / ('tests/' + test + '.cpp')), '-o', str(executable)]
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)
