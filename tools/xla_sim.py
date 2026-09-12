"""xla_sim.py — симулятор XLA VM на Python (1:1 с src/xla_vm.cpp).

Назначение: отладка .xla-игр БЕЗ платы. Отрисовка — в терминал
(ASCII-канвас 128x64), ввод — скриптованный/интерактивный.

Семантика скопирована из прошивки v0.12:
  * стек int16, GLOAD/GSTORE/GSTOREI/GLOADI/GCPY c границами;
  * JMP/JZ/JNZ/CALL/RET (rel16 от PC после операнда);
  * FRAME — граница кадра (возврат из step);
  * SAVE/LOAD -> dict рекордов (title_key);
  * RAND — детерминированный (seed) для воспроизводимых тестов;
  * MSEC — виртуальное время (1 опкод = 1 мс условно);
  * input: скрипт событий по кадрам.
"""

import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_opcodes import KIND, NAMES  # noqa: E402

W, H = 128, 64


def _f2i16(v: float) -> int:
    """C-каст (int16_t)(float) для симулятора.

    На плате аргумент всегда конечен (int16-домен), но сим не должен
    падать на аномальном входе: int(inf)/int(nan) в Python бросают,
    в C был бы мусор — возвращаем 0 (безопасный эквивалент UB).
    """
    try:
        return int(v)
    except (ValueError, OverflowError):
        return 0


class Canvas:
    """Монохромный канвас 128x64: 0/1 (+ c=0 стирает)."""

    def __init__(self) -> None:
        self.px = [[0] * W for _ in range(H)]

    def clear(self) -> None:
        self.px = [[0] * W for _ in range(H)]

    def point(self, x: int, y: int, c: int) -> None:
        if 0 <= x < W and 0 <= y < H:
            self.px[y][x] = 1 if c else 0

    def rect(self, x: int, y: int, w: int, h: int, c: int) -> None:
        if w <= 0 or h <= 0:
            return
        if c:
            for yy in range(y, y + h):
                for xx in range(x, x + w):
                    self.point(xx, yy, 1)
        else:
            for yy in range(y, y + h):
                for xx in range(x, x + w):
                    self.point(xx, yy, 0)

    def frame(self, x: int, y: int, w: int, h: int, c: int) -> None:
        if w <= 0 or h <= 0:
            return
        for xx in range(x, x + w):
            self.point(xx, y, c)
            self.point(xx, y + h - 1, c)
        for yy in range(y, y + h):
            self.point(x, yy, c)
            self.point(x + w - 1, yy, c)

    def line(self, x0: int, y0: int, x1: int, y1: int, c: int) -> None:
        dx, dy = abs(x1 - x0), abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx - dy
        while True:
            self.point(x0, y0, c)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 > -dy:
                err -= dy
                x0 += sx
            if e2 < dx:
                err += dx
                y0 += sy

    def circle(self, x: int, y: int, r: int, c: int, fill: bool = False) -> None:
        if r < 0:
            return
        if fill:
            for yy in range(y - r, y + r + 1):
                half = _f2i16(math.sqrt(max(r * r - (yy - y) ** 2, 0)))
                for xx in range(x - half, x + half + 1):
                    self.point(xx, yy, c)
        else:
            for a in range(0, 360, 2):
                rad = math.radians(a)
                self.point(
                    x + _f2i16(r * math.cos(rad)), y + _f2i16(r * math.sin(rad)), c
                )

    def ellipse(self, x: int, y: int, rx: int, ry: int, c: int) -> None:
        for a in range(0, 360, 2):
            rad = math.radians(a)
            self.point(
                x + _f2i16(rx * math.cos(rad)), y + _f2i16(ry * math.sin(rad)), c
            )

    def text(self, x: int, y: int, size: int, s: str) -> None:
        # упрощённый 5x7 глиф по клеткам size (для отладки достаточно)
        for i in range(min(len(s), 20)):
            cx = x + i * 6 * size
            self.frame(cx, y, 5 * size, 7 * size, 1)

    def dump(self) -> str:
        rows = []
        for y in range(0, H, 2):
            rows.append("".join("#" if self.px[y][x] else "." for x in range(0, W, 2)))
        return "\n".join(rows)


class VmError(Exception):
    pass


class Sim:
    def __init__(self, blob: bytes, seed: int = 1234) -> None:
        if blob[:4] != b"XLA1":
            raise VmError("not XLA1")
        code_sz = struct.unpack_from("<H", blob, 6)[0]
        data_sz = struct.unpack_from("<H", blob, 8)[0]
        str_sz = struct.unpack_from("<H", blob, 10)[0]
        self.entry = struct.unpack_from("<H", blob, 12)[0]
        tl = struct.unpack_from("<H", blob, 14)[0]
        self.title = blob[16 : 16 + tl].decode("utf-8", "replace")
        off = 16 + tl
        self.code = blob[off : off + code_sz]
        off += code_sz
        self.data = (
            list(struct.unpack_from(f"<{data_sz // 2}h", blob, off)) if data_sz else [0]
        )
        off += data_sz
        self.str = blob[off : off + str_sz]

        self.pc = self.entry
        self.stack: list[int] = []
        self.returns: list[int] = []
        self.err = ""
        self.err_pc = 0
        self.running = True
        self.exited = False
        self.frame_count = 0
        self.msec = 0
        self.rng_state = seed & 0xFFFFFFFF or 1
        self.scores: dict[str, int] = {}
        self.canvas = Canvas()
        self.event = 0  # текущее событие (скриптованное)
        self.hold = 0
        self.ops_total = 0
        self.beeps: list[tuple[int, int]] = []  # (freq, ms) — аудит звука

    # --- примитивы ---
    def _push(self, v: int) -> None:
        if len(self.stack) >= 96:
            raise VmError("stack ovf")
        # как в C: int16_t — храним знаковое значение
        self.stack.append(self._s16(v & 0xFFFF))

    def _pop(self) -> int:
        if not self.stack:
            raise VmError("stack und")
        return self.stack.pop()

    def _s16(self, v: int) -> int:
        return v - 0x10000 if v & 0x8000 else v

    def _f8(self) -> int:
        if self.pc >= len(self.code):
            raise VmError("PC OOB")
        b = self.code[self.pc]
        self.pc += 1
        return b

    def _f16(self) -> int:
        lo = self._f8()
        hi = self._f8()
        return self._s16((hi << 8) | lo)

    def _str_at(self, off: int) -> str:
        if off >= len(self.str):
            raise VmError("str OOB")
        end = self.str.find(b"\x00", off)
        if end < 0:
            end = len(self.str)
        return self.str[off:end].decode("utf-8", "replace")

    def _g_ok(self, idx: int) -> bool:
        return 0 <= idx < len(self.data)

    def _rand(self, m: int) -> int:
        # xorshift32 — детерминированный
        x = self.rng_state
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.rng_state = x
        return (x % m) if m > 0 else 0

    # --- кадр: опкоды до FRAME/ошибки/бюджета ---
    def step(self, event: int = 0, budget: int = 6000) -> str:
        """Один кадр. Возвращает 'frame' | 'halt' | 'exit'."""
        self.event = event
        self.ops_total += 1
        self.msec += 16  # ~60 fps
        n = 0
        while n < budget:
            n += 1
            if self.pc >= len(self.code):
                return "halt"
            op = self.code[self.pc]
            self.pc += 1
            mnem = NAMES.get(op)
            if mnem is None:
                self.err = f"bad op {op:02X}"
                self.err_pc = self.pc
                raise VmError(self.err)
            m = mnem.lower()
            kind = KIND[m]

            if m == "halt":
                return "halt"
            if m == "frame":
                self.frame_count += 1
                return "frame"
            if m == "exit":
                self.exited = True
                return "exit"

            if kind == "IMM16":
                v = self._f16()
                if m == "push":
                    self._push(v)
                elif m == "gstore":
                    if self._g_ok(v):
                        self.data[v] = self._pop()
                    else:
                        raise VmError("g OOB")
                elif m == "gload":
                    if self._g_ok(v):
                        self._push(self.data[v])
                    else:
                        raise VmError("g OOB")
                else:  # jmp/jz/jnz/call
                    if m == "jmp":
                        self.pc += v
                    elif m == "jz":
                        if self._pop() == 0:
                            self.pc += v
                    elif m == "jnz":
                        if self._pop() != 0:
                            self.pc += v
                    elif m == "call":
                        self.returns.append(self.pc)
                        self.pc += v
            elif kind == "STR16":
                soff = struct.unpack("<H", struct.pack("<h", self._f16()))[0]
                if m == "save":
                    v = self._pop()
                    self.scores[f"{self.title}_{self._str_at(soff)}"] = v
                elif m == "load":
                    key = f"{self.title}_{self._str_at(soff)}"
                    self._push(self.scores.get(key, self._pop()))
                elif m == "text":
                    f = self._pop()
                    y = self._pop()
                    x = self._pop()
                    self.canvas.text(x, y, f, self._str_at(soff))
                else:
                    raise VmError(f"sim: {m} не поддержан (нет сети)")
            else:
                # стековые
                if m == "dup":
                    v = self._pop()
                    self._push(v)
                    self._push(v)
                elif m == "drop":
                    self._pop()
                elif m == "swap":
                    b = self._pop()
                    a = self._pop()
                    self._push(b)
                    self._push(a)
                elif m == "over":
                    b = self._pop()
                    a = self._pop()
                    self._push(a)
                    self._push(b)
                    self._push(a)
                elif m == "pick":
                    nn = self._pop()
                    if 0 <= nn < len(self.stack):
                        self._push(self.stack[-1 - nn])
                    else:
                        self._push(0)
                elif m in (
                    "add",
                    "sub",
                    "mul",
                    "div",
                    "mod",
                    "min",
                    "max",
                    "eq",
                    "ne",
                    "lt",
                    "le",
                    "gt",
                    "ge",
                    "and",
                    "or",
                    "xor",
                ):
                    b = self._pop()
                    a = self._pop()
                    if m == "add":
                        r = a + b
                    elif m == "sub":
                        r = a - b
                    elif m == "mul":
                        r = a * b
                    elif m == "div":
                        if b == 0:
                            raise VmError("div0")
                        # C-деление (усечение к нулю)
                        r = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)
                    elif m == "mod":
                        if b == 0:
                            raise VmError("mod0")
                        r = a - b * (
                            abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)
                        )
                    elif m == "min":
                        r = min(a, b)
                    elif m == "max":
                        r = max(a, b)
                    elif m == "eq":
                        r = 1 if a == b else 0
                    elif m == "ne":
                        r = 1 if a != b else 0
                    elif m == "lt":
                        r = 1 if a < b else 0
                    elif m == "le":
                        r = 1 if a <= b else 0
                    elif m == "gt":
                        r = 1 if a > b else 0
                    elif m == "ge":
                        r = 1 if a >= b else 0
                    elif m == "and":
                        r = 1 if a != 0 and b != 0 else 0
                    elif m == "or":
                        r = 1 if a != 0 or b != 0 else 0
                    else:
                        r = 1 if (a != 0) != (b != 0) else 0
                    self._push(self._s16(r & 0xFFFF))
                elif m == "neg":
                    self._push(self._s16(-self._pop() & 0xFFFF))
                elif m == "abs":
                    v = self._pop()
                    self._push(abs(v))
                elif m == "not":
                    self._push(1 if self._pop() == 0 else 0)
                elif m == "gstorei":
                    idx = self._pop()
                    v = self._pop()
                    if self._g_ok(idx):
                        self.data[idx] = v
                    else:
                        raise VmError("g OOB")
                elif m == "gloadi":
                    idx = self._pop()
                    if self._g_ok(idx):
                        self._push(self.data[idx])
                    else:
                        raise VmError("g OOB")
                elif m == "gcpy":
                    n_ = self._pop()
                    dst = self._pop()
                    src = self._pop()
                    if (
                        n_ < 0
                        or src < 0
                        or dst < 0
                        or src + n_ > len(self.data)
                        or dst + n_ > len(self.data)
                    ):
                        raise VmError("gcpy OOB")
                    if dst > src:
                        for i in range(n_ - 1, -1, -1):
                            self.data[dst + i] = self.data[src + i]
                    else:
                        for i in range(n_):
                            self.data[dst + i] = self.data[src + i]
                elif m == "ret":
                    if not self.returns:
                        raise VmError("ret und")
                    self.pc = self.returns.pop()
                elif m == "px":
                    c = self._pop()
                    y = self._pop()
                    x = self._pop()
                    self.canvas.point(x, y, c)
                elif m == "line":
                    c = self._pop()
                    y1 = self._pop()
                    x1 = self._pop()
                    y0 = self._pop()
                    x0 = self._pop()
                    self.canvas.line(x0, y0, x1, y1, c)
                elif m == "rect":
                    c = self._pop()
                    h = self._pop()
                    w_ = self._pop()
                    y = self._pop()
                    x = self._pop()
                    self.canvas.frame(x, y, w_, h, c)
                elif m == "frect":
                    c = self._pop()
                    h = self._pop()
                    w_ = self._pop()
                    y = self._pop()
                    x = self._pop()
                    self.canvas.rect(x, y, w_, h, c)
                elif m == "circ":
                    c = self._pop()
                    r = self._pop()
                    y = self._pop()
                    x = self._pop()
                    self.canvas.circle(x, y, r, c)
                elif m == "fcirc":
                    c = self._pop()
                    r = self._pop()
                    y = self._pop()
                    x = self._pop()
                    self.canvas.circle(x, y, r, c, fill=True)
                elif m == "ell":
                    c = self._pop()
                    ry = self._pop()
                    rx = self._pop()
                    y = self._pop()
                    x = self._pop()
                    self.canvas.ellipse(x, y, rx, ry, c)
                elif m == "inv":
                    pass  # визуально в терминале не важно
                elif m == "fill":
                    self.canvas.rect(0, 0, W, H, self._pop())
                elif m == "cls":
                    self.canvas.clear()
                elif m == "disp":
                    pass  # кадр рисуется — dump() смотрим в тестах
                elif m == "msec":
                    self._push(self.msec & 0x7FFF)
                elif m == "rand":
                    self._push(self._rand(self._pop()))
                elif m == "beep":
                    ms = self._pop()
                    f = self._pop()
                    self.beeps.append((f, ms))
                elif m == "log":
                    self._pop()
                elif m == "num":
                    v = self._pop()
                    f = self._pop()
                    y = self._pop()
                    x = self._pop()
                    self.canvas.text(x, y, f, str(v))
                elif m == "delay":
                    self._pop()
                elif m == "stx" or m == "sty":
                    self._push(2048)
                elif m == "stick":
                    self._push(-1)
                elif m == "event":
                    self._push(self.event)
                elif m == "hold":
                    self._push(self.hold)
                elif m == "sin":
                    a = self._pop()
                    self._push(_f2i16(math.sin(math.radians(a)) * 1000))
                elif m == "cos":
                    a = self._pop()
                    self._push(_f2i16(math.cos(math.radians(a)) * 1000))
                elif m == "sqrt":
                    v = self._pop()
                    self._push(0 if v <= 0 else _f2i16(math.sqrt(v) + 0.5))
                else:
                    raise VmError(f"sim: не реализован {m}")
        return "budget"


def run_frames(blob: bytes, events: list[int], seed: int = 1234) -> Sim:
    """Прогнать кадры по скрипту событий. events[i] — EV на кадр i."""
    sim = Sim(blob, seed)
    for ev in events:
        r = sim.step(ev)
        if r in ("halt", "exit"):
            break
    return sim


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: python xla_sim.py app.xla")
        sys.exit(2)
    sim = Sim(Path(sys.argv[1]).read_bytes())
    # интерактив: 100 кадров без событий
    for i in range(100):
        r = sim.step(0)
        if r != "frame":
            print(f"->{r} на кадре {i}")
            break
    print(f"кадров: {sim.frame_count}, ops: {sim.ops_total}")
    print(sim.canvas.dump())
