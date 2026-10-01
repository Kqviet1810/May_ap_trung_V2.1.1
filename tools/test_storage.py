"""Compile real journal/backend/driver against deterministic fault-injection HALs."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default='g++')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='mayap-storage-') as folder:
    target = Path(folder)
    source = (root / 'MAYAP_INDUSTRIAL_v4_0_0/dual_storage.h').read_text(encoding='utf-8')
    (target / 'actual-storage.inc').write_text(source, encoding='utf-8')
    machine = (root / 'MAYAP_INDUSTRIAL_v4_0_0/machine_control.h').read_text(encoding='utf-8')
    payloads = []
    for name in ('PackedMachineConfigV1', 'PackedBatchV1'):
        start = machine.index('struct ' + name + ' {')
        payloads.append(machine[start:machine.index('};', start) + 2])
    (target / 'actual-payloads.inc').write_text('#pragma pack(push, 1)\n' +
                                             '\n'.join(payloads) + '\n#pragma pack(pop)\n', encoding='utf-8')
    driver = machine[machine.index('class ExternalEeprom24xx {'):machine.index('static_assert(sizeof(ConfigRecordV1)')]
    (target / 'actual-driver.inc').write_text(driver, encoding='utf-8')
    rtc = machine[machine.index('class RtcDs3231 {'):machine.index('// SANITIZE CAU HINH')]
    (target / 'actual-storage-rtc.inc').write_text(rtc, encoding='utf-8')
    history = (root / 'MAYAP_INDUSTRIAL_v4_0_0/history_store.h').read_text(encoding='utf-8')
    history = '\n'.join(line for line in history.splitlines() if not line.startswith('#include'))
    (target / 'actual-history.inc').write_text(history, encoding='utf-8')
    executables = []
    for name in ('storage-journal', 'storage-backend', 'storage-driver', 'storage-history', 'storage-rtc'):
        executable = target / (name + ('.exe' if os.name == 'nt' else ''))
        command = [args.cxx, '-std=c++17' if name == 'storage-rtc' else '-std=c++11', '-Wall', '-Wextra', '-Werror', '-I', str(target),
                   str(root / 'tests' / (name + '.cpp')), '-o', str(executable)]
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True)
        executables.append(executable)
    for executable in executables:
        subprocess.run([str(executable)], check=True)
