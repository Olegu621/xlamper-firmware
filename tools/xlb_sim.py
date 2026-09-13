"""xlb_sim.py — симулятор XLA BASIC на ПК (та же семантика, что xlbasic.cpp).

Отладка .xlb-игр без железа: события скриптуются, canvas — ASCII.
См. xlbasic.cpp (истина — там; здесь зеркальная семантика для QA).
"""

from __future__ import annotations

import sys

W, H = 128, 64


class Canvas:
    def __init__(self) -> None:
        self.px = [[0] * W for _ in range(H)]

    def pset(self, x, y, c):
        if 0 <= x < W and 0 <= y < H:
            self.px[y][x] = c & 1

    def line(self, x1, y1, x2, y2, c):
        dx, dy = abs(x2 - x1), abs(y2 - y1)
        sx = 1 if x1 < x2 else -1
        sy = 1 if y1 < y2 else -1
        err = dx - dy
        while True:
            self.pset(x1, y1, c)
            if x1 == x2 and y1 == y2:
                break
            e2 = err * 2
            if e2 > -dy:
                err -= dy
                x1 += sx
            if e2 < dx:
                err += dx
                y1 += sy

    def rect(self, x, y, w, h, c, fill=False):
        if w <= 0 or h <= 0:
            return
        if fill:
            for yy in range(max(0, y), min(H, y + h)):
                for xx in range(max(0, x), min(W, x + w)):
                    self.pset(xx, yy, c)
        else:
            self.line(x, y, x + w - 1, y, c)
            self.line(x, y + h - 1, x + w - 1, y + h - 1, c)
            self.line(x, y, x, y + h - 1, c)
            self.line(x + w - 1, y, x + w - 1, y + h - 1, c)

    def circ(self, cx, cy, r, c, fill=False):
        if r < 0:
            return
        for yy in range(max(0, cy - r), min(H, cy + r + 1)):
            for xx in range(max(0, cx - r), min(W, cx + r + 1)):
                d = (xx - cx) ** 2 + (yy - cy) ** 2
                if fill and d <= r * r or (not fill and r * r - 2 * r <= d <= r * r):
                    self.pset(xx, yy, c)

    def show(self) -> str:
        out = []
        for y in range(0, H, 2):
            row = "".join(
                "#" if any(self.px[y + k][x] for k in (0, 1) if y + k < H) else " "
                for x in range(W)
            )
            out.append(row)
        return "\n".join(out)


class XlbSim:
    """Зеркало xlbasic.cpp: те же ключевые слова/семантика для QA."""

    MAXSTEPS = 200000

    def __init__(self, src: str, seed: int = 1) -> None:
        self.title = "APP"
        self.lines: list[str] = []
        self.labels: dict[str, int] = {}
        self.vars = [0] * 702
        self.nvs: dict[str, int] = {}
        self.beeps: list[tuple[int, int]] = []
        self.cur_event = 0
        self.ended = False
        self.err: str | None = None
        self.err_line = 0
        self.frames = 0
        self._rng_state = seed & 0xFFFFFFFF or 1

        body = []
        for raw in src.splitlines():
            ln = raw.rstrip("\r").strip()
            if not ln or ln.startswith("'") or ln.upper().startswith("REM"):
                body.append("")
                continue
            if not self.lines and ln.upper().startswith("TITLE:"):
                self.title = ln[6:].strip()[:12]
                body.append("")
                continue
            # метка
            import re

            m = re.match(r"^([A-Za-z]+):\s*(.*)$", ln)
            if m and m.group(1).upper() not in (
                "IF",
                "TITLE",
            ):
                self.labels[m.group(1).upper()] = len(body)
                ln = m.group(2)
            body.append(ln)
        self.lines = body

    # xorshift32 — как esp_random по духу
    def _rand(self, n: int) -> int:
        x = self._rng_state
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self._rng_state = x
        return (x % n) if n > 0 else 0

    # ---------- выражения ----------
    def _var_index(self, name: str) -> int:
        n = name.upper()
        if len(n) == 1:
            return ord(n) - 65
        if len(n) == 2:
            return 26 + (ord(n[0]) - 65) * 26 + (ord(n[1]) - 65)
        return -1

    def _eval(self, expr: str, line_no: int) -> int:
        import re

        s = expr.strip()
        # сравнения (двухсимвольные первыми — парные!)
        for op, fn in (
            ("==", lambda a, b: a == b),
            ("!=", lambda a, b: a != b),
            ("<=", lambda a, b: a <= b),
            (">=", lambda a, b: a >= b),
            ("<", lambda a, b: a < b),
            (">", lambda a, b: a > b),
        ):
            i = self._find_top(s, op)
            if i >= 0:
                return (
                    1
                    if fn(
                        self._eval(s[:i], line_no),
                        self._eval(s[i + len(op) :], line_no),
                    )
                    else 0
                )
        # сложение/вычитание
        i = self._find_top(s, "+|-")
        if i > 0:
            a = self._eval(s[:i], line_no)
            b = self._eval(s[i + 1 :], line_no)
            return a + b if s[i] == "+" else a - b
        # умножение/деление
        i = self._find_top(s, "*|/|%")
        if i > 0:
            a = self._eval(s[:i], line_no)
            b = self._eval(s[i + 1 :], line_no)
            if s[i] == "*":
                return a * b
            if b == 0:
                raise self._err(line_no, "div0")
            return a // b if s[i] == "/" else a % b
        # унарный минус
        if s.startswith("-"):
            return -self._eval(s[1:], line_no)
        # скобки
        if s.startswith("(") and s.endswith(")"):
            inner = s[1:-1]
            if self._balanced(inner):
                return self._eval(inner, line_no)
        # число
        if re.fullmatch(r"\d+", s):
            try:
                v = int(s)
            except ValueError:  # недостижимо после fullmatch, но парсим в одном месте
                raise self._err(line_no, f"syntax: {s!r}") from None
            if v > 32767:
                raise self._err(line_no, "int16 ovf")
            return v
        # переменная
        m = re.fullmatch(r"([A-Za-z]{1,2})", s)
        if m:
            idx = self._var_index(m.group(1))
            if idx < 0:
                raise self._err(line_no, "bad var")
            return self.vars[idx]
        raise self._err(line_no, f"syntax: {s!r}")

    def _find_top(self, s: str, ops: str) -> int:
        """Индекс оператора вне скобок; -1 если нет.
        ops — набор ОПЕРАТОРОВ через |: '==' значит пару, '+-' любой из них.
        Двухсимвольные операторы матчатся ЦЕЛИКОМ (нужен ОПЕРАТОР, не его символ)."""
        two = [o for o in ops.split("|") if len(o) == 2]
        one = [o for o in ops.split("|") if len(o) == 1]
        depth = 0
        i = 0
        while i < len(s):
            c = s[i]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            elif depth == 0:
                matched_len = 0
                for t in two:
                    if s.startswith(t, i):
                        matched_len = 2
                        break
                if not matched_len and c in one:
                    # унарный минус
                    if c == "-" and (i == 0 or s[i - 1] in "+-*/%(<>=! "):
                        i += 1
                        continue
                    matched_len = 1
                if matched_len:
                    return i
            i += 1
        return -1

    def _balanced(self, s: str) -> bool:
        d = 0
        for c in s:
            if c == "(":
                d += 1
            elif c == ")":
                d -= 1
            if d < 0:
                return False
        return d == 0

    def _err(self, line_no: int, msg: str) -> Exception:
        self.err = f"{msg} (line {line_no})"
        self.err_line = line_no
        return RuntimeError(self.err)

    # ---------- исполнение ----------
    def step(self, event: int = 0, max_steps: int = 200000) -> str:
        """Кадр: до следующего WAIT. 'wait' | 'end' | 'err'."""
        self.cur_event = event
        steps = 0
        pc = getattr(self, "_pc", 0)
        if not hasattr(self, "_ret"):
            self._ret = []  # стек GOSUB (глубина 8)
        while steps < max_steps:
            steps += 1
            if pc >= len(self.lines):
                return "end"
            ln = self.lines[pc]
            pc += 1
            if "'" in ln:
                ln = ln[: ln.index("'")]
            if not ln.strip():
                continue
            r, pc = self._exec(ln, pc, None, pc)
            if r == "wait":
                self._pc = pc
                self.frames += 1
                return "wait"
            if r == "end":
                self._pc = pc
                self.ended = True
                return "end"
        self._pc = pc
        return "timeout"

    def _exec(self, ln: str, pc: int, ret, line_no: int):
        import re

        u = ln.upper()
        if u.startswith("END"):
            return "end", pc
        if u.startswith("CLS"):
            self.cv = Canvas()
            return "ok", pc
        if u.startswith("DISP"):
            return "disp", pc  # не кадр; шаг
        if u.startswith("BEEP "):
            m = re.match(r"BEEP\s+(.+?)\s*,\s*(.+)$", ln, re.I)
            if not m:
                raise self._err(line_no, "BEEP f ms")
            f = self._eval(m.group(1), line_no)
            ms = self._eval(m.group(2), line_no)
            self.beeps.append((f, ms))
            return "ok", pc
        if u.startswith("WAIT"):
            return "wait", pc
        if u.startswith("TEXT "):
            m = re.match(r'TEXT\s+(\S+)\s+(\S+)\s+"(.*)"', ln, re.I)
            if not m:
                raise self._err(line_no, 'TEXT x y "текст"')
            # в симе текст не рисуем (ASCII-канва), но валидируем координаты
            self._eval(m.group(1), line_no)
            self._eval(m.group(2), line_no)
            return "ok", pc
        if u.startswith("NUM "):
            m = re.match(r"NUM\s+(\S+)\s+(\S+)\s+(.+)$", ln, re.I)
            if not m:
                raise self._err(line_no, "NUM x y v")
            self._eval(m.group(1), line_no)
            self._eval(m.group(2), line_no)
            self._eval(m.group(3), line_no)
            return "ok", pc
        if u.startswith(("PSET ", "LINE ", "RECT ", "FRECT ", "CIRC ", "FCIRC ")):
            mm = re.match(r"([A-Z]+)\s+(.+)$", u)
            if mm is None:
                raise self._err(line_no, f"графика: {ln[:20]!r}")
            parts = mm.group(2).split()
            vals = [self._eval(v, line_no) for v in parts]
            if u.startswith("PSET"):
                self.cv.pset(*vals[:3])
            elif u.startswith("LINE"):
                self.cv.line(*vals[:5])
            elif u.startswith("RECT"):
                self.cv.rect(vals[0], vals[1], vals[2], vals[3], vals[4], fill=False)
            elif u.startswith("FRECT"):
                self.cv.rect(vals[0], vals[1], vals[2], vals[3], vals[4], fill=True)
            elif u.startswith("CIRC"):
                self.cv.circ(vals[0], vals[1], vals[2], vals[3])
            elif u.startswith("FCIRC"):
                self.cv.circ(vals[0], vals[1], vals[2], vals[3], fill=True)
            return "ok", pc
        if u.startswith("STICK"):
            m = re.match(r"STICK\s*->\s*([A-Za-z]{1,2})", ln, re.I)
            if not m:
                raise self._err(line_no, "STICK -> V")
            self.vars[self._var_index(m.group(1))] = -1  # сим: покой
            return "ok", pc
        if u.startswith(("KEY", "EVENT")):
            m = re.match(r"(?:KEY|EVENT)\s*->\s*([A-Za-z]{1,2})", ln, re.I)
            if not m:
                raise self._err(line_no, "KEY -> V")
            idx = self._var_index(m.group(1))
            self.vars[idx] = self.cur_event
            self.cur_event = 0
            return "ok", pc
        m = re.match(r"RND\s+(.+?)\s*->\s*([A-Za-z]{1,2})", ln, re.I)
        if m:
            n = self._eval(m.group(1), line_no)
            self.vars[self._var_index(m.group(2))] = self._rand(n) if n > 0 else 0
            return "ok", pc
        m = re.match(r"SCORE\s+(\S+)\s*->\s*([A-Za-z]{1,2})", ln, re.I)
        if m:
            key = f"{self.title}_{m.group(1)}"
            self.vars[self._var_index(m.group(2))] = self.nvs.get(key, 0)
            return "ok", pc
        m = re.match(r"SCORE\s+(\S+)\s+(.+)$", ln, re.I)
        if m:
            v = self._eval(m.group(2), line_no)
            self.nvs[f"{self.title}_{m.group(1)}"] = v
            return "ok", pc
        m = re.match(r"(GOTO|GOSUB)\s+([A-Za-z]+)", ln, re.I)
        if m:
            tgt = m.group(2).upper()
            if tgt not in self.labels:
                raise self._err(line_no, f"нет метки {tgt}")
            if m.group(1).upper() == "GOSUB":
                if len(self._ret) >= 8:
                    raise self._err(line_no, "GOSUB глубина > 8")
                self._ret.append(line_no)  # возврат: строка после GOSUB
            return "ok", self.labels[tgt]
        if u.startswith("RETURN"):
            if not self._ret:
                raise self._err(line_no, "RETURN без GOSUB")
            return "ok", self._ret.pop()   # GOSUB уже пушил индекс СЛЕДУЮЩЕЙ строки
        # IF cond THEN cmd
        m = re.match(r"IF\s+(.+?)\s+THEN\s+(.+)$", ln, re.I)
        if m:
            if self._eval(m.group(1), line_no):
                r, pc2 = self._exec(m.group(2), pc, None, line_no)
                return r, pc2  # переходы (GOTO/GOSUB/RETURN) сквозь!
            return "ok", pc
        # присваивание [LET] V = expr
        m = re.match(r"(?:LET\s+)?([A-Za-z]{1,2})\s*=\s*(.+)$", ln)
        if m:
            idx = self._var_index(m.group(1))
            if idx < 0:
                raise self._err(line_no, "bad var")
            v = self._eval(m.group(2), line_no)
            self.vars[idx] = v & 0xFFFF if v >= 0 else v
            if not -32768 <= v <= 32767:
                self.vars[idx] = ((v + 32768) % 65536) - 32768  # int16 wrap
            return "ok", pc
        raise self._err(line_no, f"неизвестно: {ln[:30]!r}")


def main() -> None:
    if len(sys.argv) < 2:
        print("usage: python xlb_sim.py app.xlb")
        sys.exit(2)
    from pathlib import Path

    src = Path(sys.argv[1]).read_text(encoding="utf-8")
    sim = XlbSim(src)
    # прогон: кадры с событиями OK (5)
    script = [0] * 5 + [5] + [0] * 5 + [5] + [0] * 20 + [6]  # 6 = EXIT-выход
    try:
        for ev in script:
            r = sim.step(ev, max_steps=20000)
            if r == "end":
                break
        print(f"кадров: {sim.frames}, beep: {len(sim.beeps)}, err: {sim.err or 'нет'}")
        print(f"nvs: {sim.nvs}")
    except RuntimeError as e:
        print(f"ОШИБКА: {e}")


if __name__ == "__main__":
    main()
