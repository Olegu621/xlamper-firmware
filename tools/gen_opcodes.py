"""gen_opcodes.py — генератор таблицы опкодов из ЕДИНОГО источника правды:
заголовок прошивки xla_opcodes.h (X-макро-таблица).

Все инструменты (xlas.py, xla_disasm.py, симулятор) импортируют таблицу
ОТСЮДА. Рассинхрон «VM знает одно — ассемблер другое» (баг v0.11 с
GCPY/HTTP/DELAY) становится структурно невозможным: одна таблица.

Поиск заголовка (первый найденный):
    1. env XLA_OPS_HEADER — явный путь
    2. ./xla_opcodes.h    — копия рядом с инструментами (самодостаточный репо)
    3. ../src/xla_opcodes.h — если инструменты лежат в прошивке

Выход:
    OPS        — {mnemonic: opcode}
    KIND       — {mnemonic: 'NONE'|'IMM16'|'STR16'}
    NAMES      — {opcode: mnemonic}
"""

import os
import re
from pathlib import Path

_HERE = Path(__file__).resolve().parent


def _find_header() -> Path:
    """Найти xla_opcodes.h: env -> рядом -> ../src (прошивка)."""
    env = os.environ.get("XLA_OPS_HEADER")
    if env:
        p = Path(env)
        if p.exists():
            return p
    local = _HERE / "xla_opcodes.h"
    if local.exists():
        return local
    fw = _HERE.parent / "src" / "xla_opcodes.h"
    if fw.exists():
        return fw
    raise FileNotFoundError(
        "xla_opcodes.h не найден: положи копию рядом с tools/, "
        "укажи XLA_OPS_HEADER или запускай из репо прошивки"
    )


HEADER = _find_header()

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
