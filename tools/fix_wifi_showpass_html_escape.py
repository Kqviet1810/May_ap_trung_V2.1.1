from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
network_path = ROOT / "MAYAP_INDUSTRIAL_v3_4_0/network_service.h"
text = network_path.read_text(encoding="utf-8")
lines = text.splitlines()

matches = [i for i, line in enumerate(lines) if '"onchange=' in line and 'wifiPassword' in line]
if len(matches) != 1:
    raise SystemExit(f"FAIL: expected exactly one show-password onchange line, got {len(matches)}")

i = matches[0]
lines[i] = r'''    "onchange=\"document.getElementById('wifiPassword').type=this.checked?'text':'password'\">"'''
network_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

fixed = network_path.read_text(encoding="utf-8")
expected = r'''"onchange=\"document.getElementById('wifiPassword').type=this.checked?'text':'password'\">"'''
if expected not in fixed:
    raise SystemExit("FAIL: corrected show-password HTML/JS not found")
if r'''"onchange=\\\"''' in fixed:
    raise SystemExit("FAIL: show-password attribute is still double-escaped")
print("Portal show-password HTML escaping corrected")
