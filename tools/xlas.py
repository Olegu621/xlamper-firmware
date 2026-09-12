"""xlas.py — ассемблер XLA-байткода для C3 XLAMPER v0.12.

Таблица опкодов импортируется из gen_opcodes.py, который парсит
ЕДИНСТВЕННЫЙ источник правды — src/xla_opcodes.h. Рассинхрон
«ассемблер знает GCPY, а VM нет» (баг v0.11) стал структурно
невозможен: одна таблица на всё.

Формат .xlas (текст, по инструкции на строку):
    ; комментарий
    .title NAME     ; 1..12 симв (NVS-ключи, заголовок)
    .data N         ; N int16-глобалов (0..2048)
    .str NAME text  ; именованная строка в пул
    label:          ; метка
    push 42         ; IMM16 (int16)
    jmp/jz/jnz/call label   ; rel16
    gstore/gload IDX ; IMM16
    text X Y SIZE STRNAME    ; push x3 + STR16
    save STRNAME    ; STR16 (значение на стеке)
    load STRNAME DEF ; push DEF + STR16
    px/line/rect/... ; стековые: "rect 10 20 5 5 1" == push-и + RECT

Порядок операндов стековый: значения кладутся как пишутся,
VM-опкод снимает их в обратном порядке.
"""

import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_opcodes import KIND, OPS  # noqa: E402


class AsmError(Exception):
    pass


IMM16_MNEMS = {m for m, k in KIND.items() if k == "IMM16"}  # push/gstore/gload/jmp...
STR16_MNEMS = {m for m, k in KIND.items() if k == "STR16"}  # text/save/load/httpget
JUMP_MNEMS = {"jmp", "jz", "jnz", "call"}


def _int_or_err(s: str, lineno: int, what: str) -> int:
    try:
        return int(s, 0)
    except ValueError:
        raise AsmError(f"{lineno}: {what} — не число: {s!r}") from None


@dataclass
class Line:
    src: str
    lineno: int
    op: str = ""
    args: list = field(default_factory=list)


@dataclass
class Program:
    title: str = "APP"
    data_slots: int = 0
    strings: dict = field(default_factory=dict)
    lines: list = field(default_factory=list)
    labels: dict = field(default_factory=dict)
    refs: list = field(default_factory=list)


def parse(src: str) -> Program:
    prog = Program()
    for ln_no, raw in enumerate(src.splitlines(), 1):
        line = raw.split(";")[0].strip()
        if not line:
            continue
        if line.startswith("."):
            parts = line.split(None, 2)
            d = parts[0]
            if d == ".title":
                if len(parts) < 2 or not (1 <= len(parts[1]) <= 12):
                    raise AsmError(f"{ln_no}: .title требует 1..12 символов")
                prog.title = parts[1]
            elif d == ".data":
                if len(parts) < 2:
                    raise AsmError(f"{ln_no}: .data требует число 0..2048")
                n = _int_or_err(parts[1], ln_no, ".data")
                if not 0 <= n <= 2048:
                    raise AsmError(f"{ln_no}: .data 0..2048, получено {n}")
                prog.data_slots = n
            elif d == ".str":
                m = re.match(r"\.str\s+(\w+)\s+(.+)$", line)
                if not m:
                    raise AsmError(f"{ln_no}: .str ИМЯ текст")
                prog.strings[m.group(1)] = m.group(2).encode("utf-8")
            else:
                raise AsmError(f"{ln_no}: неизвестная директива {d}")
            continue
        if line.endswith(":"):
            prog.lines.append(Line(src=raw, lineno=ln_no, op=f"{line[:-1]}:"))
            continue
        parts = line.split()
        mnem = parts[0].lower()
        if mnem not in OPS:
            raise AsmError(f"{ln_no}: неизвестная мнемоника {parts[0]}")
        prog.lines.append(Line(src=raw, lineno=ln_no, op=mnem, args=parts[1:]))
    return prog


def str_off(prog: Program, name: str) -> int:
    if name not in prog.strings:
        raise AsmError(f"строка {name} не объявлена")
    off = 0
    for nm, data in prog.strings.items():
        if nm == name:
            return off
        off += len(data) + 1
    raise AsmError(f"строка {name} не найдена")  # pragma: no cover


def encode(prog: Program) -> bytes:
    code = bytearray()

    def emit_push(v: int, lineno: int) -> None:
        if not -32768 <= v <= 65535:
            raise AsmError(f"{lineno}: push {v} вне int16")
        code.extend(
            bytes([OPS["push"]]) + struct.pack("<h", v if v < 32768 else v - 65536)
        )

    for ln in prog.lines:
        if ln.op.endswith(":"):
            prog.labels[ln.op[:-1]] = len(code)
            continue
        mnem = ln.op
        op = OPS[mnem]
        args = ln.args
        if mnem == "push":
            if len(args) != 1:
                raise AsmError(f"{ln.lineno}: push IMM")
            emit_push(_int_or_err(args[0], ln.lineno, "push"), ln.lineno)
        elif mnem == "pushstr":
            if len(args) != 1 or args[0] not in prog.strings:
                raise AsmError(f"{ln.lineno}: pushstr ИМЯ (строка не объявлена)")
            emit_push(str_off(prog, args[0]), ln.lineno)
        elif mnem in JUMP_MNEMS:
            if len(args) != 1:
                raise AsmError(f"{ln.lineno}: {mnem} LABEL")
            code.append(op)
            prog.refs.append((ln.lineno, len(code), args[0]))
            code += b"\x00\x00"
        elif mnem in STR16_MNEMS:
            if mnem == "text":
                if len(args) != 4:
                    raise AsmError(f"{ln.lineno}: text X Y SIZE STRNAME")
                for a in args[:3]:
                    emit_push(_int_or_err(a, ln.lineno, "text"), ln.lineno)
                code += bytes([op]) + struct.pack("<H", str_off(prog, args[3]))
            elif mnem == "save":
                if len(args) != 1:
                    raise AsmError(f"{ln.lineno}: save STRNAME")
                code += bytes([op]) + struct.pack("<H", str_off(prog, args[0]))
            elif mnem == "load":
                if len(args) != 2:
                    raise AsmError(f"{ln.lineno}: load STRNAME DEF")
                emit_push(_int_or_err(args[1], ln.lineno, "load"), ln.lineno)
                code += bytes([op]) + struct.pack("<H", str_off(prog, args[0]))
            else:  # httpget URL-строка
                if len(args) != 1:
                    raise AsmError(f"{ln.lineno}: {mnem} STRNAME")
                code += bytes([op]) + struct.pack("<H", str_off(prog, args[0]))
        elif mnem in IMM16_MNEMS:
            if len(args) != 1:
                raise AsmError(f"{ln.lineno}: {mnem} IDX (uint16)")
            idx = _int_or_err(args[0], ln.lineno, mnem)
            if not 0 <= idx <= 65535:
                raise AsmError(f"{ln.lineno}: {mnem} 0..65535, получено {idx}")
            code += bytes([op]) + struct.pack("<H", idx)
        else:
            for a in args:
                emit_push(_int_or_err(a, ln.lineno, mnem), ln.lineno)
            code.append(op)

    for lineno, off, label in prog.refs:
        if label not in prog.labels:
            raise AsmError(f"{lineno}: метка {label} не определена")
        rel = prog.labels[label] - (off + 2)
        if not -32768 <= rel <= 32767:
            raise AsmError(f"{lineno}: rel16 слишком далеко: {rel}")
        struct.pack_into("<h", code, off, rel)
    return bytes(code)


def assemble(src: str) -> bytes:
    prog = parse(src)
    code = encode(prog)
    pool = bytearray()
    for data in prog.strings.values():
        pool += data + b"\x00"
    data = bytes(prog.data_slots * 2)
    title = prog.title.encode("utf-8")[:12]
    # заголовок XLA1 (16 байт + титул):
    # magic(4) ver(1) flags(1) code(2) data(2) str(2) entry(2) titleLen(2)
    hdr = b"XLA1" + bytes([1, 0])
    hdr += struct.pack("<HHHHH", len(code), len(data), len(pool), 0, len(title))
    hdr += title
    return hdr + code + data + bytes(pool)


def main() -> None:
    if len(sys.argv) != 3:
        print("usage: python xlas.py input.xlas output.xla")
        sys.exit(2)
    try:
        src = Path(sys.argv[1]).read_text(encoding="utf-8")
        blob = assemble(src)
        Path(sys.argv[2]).write_bytes(blob)
    except OSError as exc:
        print(f"IO error: {exc}", file=sys.stderr)
        sys.exit(1)
    except AsmError as exc:
        print(f"ASSEMBLY ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
    code_sz = blob[6] | (blob[7] << 8)
    data_sz = blob[8] | (blob[9] << 8)
    str_sz = blob[10] | (blob[11] << 8)
    title_len = blob[14] | (blob[15] << 8)
    title = blob[16 : 16 + title_len]
    print(
        f"OK: {sys.argv[2]} ({len(blob)} bytes) code={code_sz} data={data_sz} str={str_sz} title={title!r}"
    )


if __name__ == "__main__":
    main()
