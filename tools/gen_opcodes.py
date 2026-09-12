"""gen_opcodes.py — генератор таблицы опкодов из ЕДИНОГО источника правды:
xlamper_v012/src/xla_opcodes.h (X-макро-таблица).

Все инструменты (xlas.py, xla_disasm.py, симулятор) импортируют таблицу
ОТСЮДА. Рассинхрон «VM знает одно — ассемблер другое» (баг v0.11 с
GCPY/HTTP/DELAY) становится структурно невозможным: одна таблица.

Выход:
    OPS        — {mnemonic: opcode}
    KIND       — {mnemonic: 'NONE'|'IMM16'|'STR16'}
    NAMES      — {opcode: mnemonic}
"""

import re
from pathlib import Path

HEADER = Path(__file__).resolve().parent.parent / "src" / "xla_opcodes.h"

_LINE = re.compile(
    r"^\s*X\(\s*(0x[0-9A-Fa-f]{2})\s*,\s*([A-Z][A-Z0-9_]*)\s*,\s*(NONE|IMM16|STR16)\s*\)",
    re.M,
)


def parse_header(path: Path | None = None) -> tuple[dict, dict, dict]:
    """Разбор X-макро-таблицы. Возвращает (OPS, KIND, NAMES)."""
    src = (path or HEADER).read_text(encoding="utf-8")
    ops: dict[str, int] = {}
    kind: dict[str, str] = {}
    names: dict[int, str] = {}
    for mm in _LINE.finditer(src):
        code = int(mm.group(1), 16)
        mnem = mm.group(2).lower()
        k = mm.group(3)
        if mnem in ops:
            raise ValueError(f"дубликат мнемоники {mnem}")
        if code in names:
            raise ValueError(f"дубликат кода 0x{code:02X} ({names[code]}/{mnem})")
        ops[mnem] = code
        kind[mnem] = k
        names[code] = mnem.upper()
    if not ops:
        raise ValueError("пустая таблица опкодов")
    return ops, kind, names


OPS, KIND, NAMES = parse_header()

if __name__ == "__main__":
    print(f"опкодов: {len(OPS)}")
    for code in sorted(NAMES):
        mnem = NAMES[code]
        print(f"  0x{code:02X}  {mnem:<8} {KIND[mnem.lower()]}")
