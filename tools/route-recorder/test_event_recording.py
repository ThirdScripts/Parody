"""Offline validation tests; never sends input or connects to CS2."""
import copy
import json
import pathlib
import subprocess
import sys
import tempfile

exe = pathlib.Path(sys.argv[1]).resolve()
def sample(t):
    return dict(type="sample", t=t, valid=True, alive=True, position=[0, 0, 0], velocity=[0, 0, 0], yaw=0, pitch=0)
base = [dict(type="header", schema=2), sample(0), sample(.02), sample(.04),
        dict(type="input", kind="begin", t=.04),
        dict(type="input", kind="key", t=.05, scan=17, extended=False, up=False),
        dict(type="input", kind="mouse", t=.08, dx=3, dy=-2, buttons=1024, wheel=-120),
        dict(type="input", kind="key", t=.1, scan=17, extended=False, up=True),
        *[sample(i / 100) for i in range(6, 22, 2)],
        dict(type="end", input_valid=True)]
cases = [("valid keys and wheel", base, True)]
def changed(name, change):
    rows = copy.deepcopy(base)
    change(rows)
    cases.append((name, rows, False))
changed("no end", lambda r: r.pop())
changed("invalid input", lambda r: r[-1].update(input_valid=False))
changed("no F7 start", lambda r: r.pop(4))
changed("invalid scan", lambda r: r[5].update(scan=0))
changed("non-game key", lambda r: r[5].update(scan=1))
changed("reverse events", lambda r: r[7].update(t=.01))
changed("unknown event", lambda r: r[6].update(kind="unknown"))
changed("focus lost", lambda r: r[6].update(kind="focus_lost"))
changed("unsupported mouse flags", lambda r: r[6].update(buttons=65535))
changed("death", lambda r: r[8].update(alive=False))
changed("position gap", lambda r: r[-2].update(t=1))
changed("missing pose", lambda r: r[8].update(position=[]))
changed("invalid numeric", lambda r: r[6].update(dx="bad"))
changed("moving at start", lambda r: r[3].update(velocity=[100, 0, 0]))
changed("missing velocity", lambda r: r[3].pop("velocity"))
changed("map switch", lambda r: (r[3].update(map="de_mirage"), r[8].update(map="de_dust2")))
finished = copy.deepcopy(base)
finished.insert(-1, dict(type="input", kind="focus_end", t=.21))
cases.append(("clean focus stop", finished, True))
legacy = copy.deepcopy(finished)
legacy[-2]["kind"] = "focus_lost"
legacy[-1]["input_valid"] = False
cases.append(("legacy invalid prefix rejected", legacy, False))
with tempfile.TemporaryDirectory() as folder:
    for i, (name, rows, accepted) in enumerate(cases):
        file = pathlib.Path(folder) / f"{i}.jsonl"
        file.write_text("\n".join(json.dumps(row) for row in rows), encoding="utf-8")
        result = subprocess.run([str(exe), "--validate", str(file), "de_mirage"], cwd=folder,
                                capture_output=True, timeout=15)
        assert (result.returncode == 0) == accepted, (name, result.returncode, result.stdout, result.stderr)
        print("PASS", name)
print(f"{len(cases)} offline cases passed")

