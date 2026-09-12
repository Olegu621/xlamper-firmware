"""lint_xlas.py — проверка .xlas на неизвестные мнемоники/директивы.
Показывает строку:номер и содержимое. Хороший/мусорный токен сразу видно."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_opcodes import OPS

DIRECTIVES = {".title", ".data", ".str"}

def lint(path: str) -> int:
    bad = 0
    for no, raw in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split(";")[0].strip()
        if not line or line.endswith(":"):
            continue
        head = line.split()[0]
        if head.startswith("."):
            if head not in DIRECTIVES:
                print(f"{no}: ДИРЕКТИВА? {line}")
                bad += 1
        elif head.lower() not in OPS:
            print(f"{no}: !!!МНЕМОНИКА {head!r}: {line}")
            bad += 1
    print(f"плохих строк: {bad}")
    return bad

if __name__ == "__main__":
    lint(sys.argv[1])
