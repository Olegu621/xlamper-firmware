"""xla_disasm.py — дизассемблер XLA-байткода для C3 XLAMPER v0.12.

Таблица опкодов — из gen_opcodes.py (единственный источник правды:
src/xla_opcodes.h). Дизассемблер теперь знает ВСЕ опкоды VM,
включая GCPY(0x49)/HTTPGET/DELAY/WGET, которых не знал дизассемблер
v0.11 (помечал их как ?49, сбивая адресацию).

Использование:
    python xla_disasm.py app.xla            # весь код
    python xla_disasm.py app.xla 100..200   # диапазон pc
    python xla_disasm.py app.xla @123       # с pc=123 до конца
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_opcodes import KIND, NAMES  # noqa: E402

JUMP_MNEMS = {"jmp", "jz", "jnz", "call"}


def load_bytes(blob: bytes) -> tuple[bytes, bytes, bytes, bytes, str]:
    """Разбор XLA1-блоба из памяти: (code, data, pool, blob, title)."""
    if blob[:4] != b"XLA1":
        raise ValueError("не XLA1-файл")
    code_sz = struct.unpack_from("<H", blob, 6)[0]
    data_sz = struct.unpack_from("<H", blob, 8)[0]
    str_sz = struct.unpack_from("<H", blob, 10)[0]
    title_len = struct.unpack_from("<H", blob, 14)[0]
    title = blob[16 : 16 + title_len].decode("utf-8", "replace")
    off = 16 + title_len
    code = blob[off : off + code_sz]
    off += code_sz
    data = blob[off : off + data_sz]
    off += data_sz
    pool = blob[off : off + str_sz]
    if len(code) != code_sz:
        raise ValueError("урезанный код")
    return code, data, pool, blob, title


def load(path: str) -> tuple[bytes, bytes, bytes, bytes, str]:
    """Разбор XLA1-файла с диска."""
    return load_bytes(Path(path).read_bytes())


def pool_string_at(pool: bytes, off: int) -> str:
    end = pool.find(b"\x00", off)
    if end < 0:
        end = len(pool)
    return pool[off:end].decode("utf-8", "replace")


def disasm(
    code: bytes, pool: bytes, start: int = 0, end: int | None = None
) -> list[str]:
    out = []
    pc = start
    stop = len(code) if end is None else min(end, len(code))
    while pc < stop:
        op = code[pc]
        mnem = NAMES.get(op)
        if mnem is None:
            out.append(f"pc={pc:4d}: ?? 0x{op:02X}   ; НЕИЗВЕСТНЫЙ ОПКОД")
            pc += 1
            continue
        kind = KIND[mnem.lower()]
        if kind == "IMM16":
            if pc + 3 > len(code):
                out.append(f"pc={pc:4d}: {mnem} <урезан>")
                break
            v = struct.unpack_from("<h", code, pc + 1)[0]
            if mnem.lower() in JUMP_MNEMS:
                target = pc + 3 + v
                out.append(f"pc={pc:4d}: {mnem:<8} -> pc {target}")
            else:
                out.append(f"pc={pc:4d}: {mnem:<8} {v}")
            pc += 3
        elif kind == "STR16":
            if pc + 3 > len(code):
                out.append(f"pc={pc:4d}: {mnem} <урезан>")
                break
            soff = struct.unpack_from("<H", code, pc + 1)[0]
            s = pool_string_at(pool, soff) if soff < len(pool) else "<OOB>"
            out.append(f'pc={pc:4d}: {mnem:<8} "{s}" (str@{soff})')
            pc += 3
        else:
            out.append(f"pc={pc:4d}: {mnem}")
            pc += 1
    return out


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    try:
        code, data, pool, blob, title = load(sys.argv[1])
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
    start, end = 0, None
    if len(sys.argv) == 3:
        arg = sys.argv[2]
        if arg.startswith("@"):
            start = int(arg[1:], 0)
        elif ".." in arg:
            a, b = arg.split("..", 1)
            start = int(a, 0) if a else 0
            end = int(b, 0) if b else None
    print(f"; {sys.argv[1]} ({len(blob)} B) title={title!r}")
    print(f"; code={len(code)} B data={len(data)} B strings={len(pool)} B")
    for line in disasm(code, pool, start, end):
        print(line)


if __name__ == "__main__":
    main()
