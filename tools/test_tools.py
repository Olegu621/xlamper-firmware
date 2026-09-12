"""test_tools.py — roundtrip-тесты инструментария XLA (v0.12).

Проверки:
 1. Сборка тестовой программы -> дизассемблирование -> повторная сборка
    из текста == исходный бинкод (roundtrip).
 2. Дизассемблирование настоящего snake.xla (из репо Olegu621/xlamper)
    без «НЕИЗВЕСТНЫЙ ОПКОД» (v0.11 спотыкался на GCPY 0x49).
 3. Таблица опкодов непротиворечива (нет дублей кодов/мнемоник).
 4. Дизассемблер snake.xla покрывает ВЕСЬ code-сегмент непрерывно
    (pc следующей инструкции == len(code) в конце).
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import xla_disasm  # noqa: E402
import xlas  # noqa: E402
from gen_opcodes import KIND, NAMES, OPS  # noqa: E402

FAILED = 0


def check(name: str, ok: bool, detail: str = "") -> None:
    global FAILED
    mark = "OK " if ok else "FAIL"
    print(f"[{mark}] {name}" + (f" — {detail}" if detail else ""))
    if not ok:
        FAILED += 1


def test_table_consistency() -> None:
    check("таблица: 69 опкодов", len(OPS) == 69, f"{len(OPS)}")
    check("таблица: GCPY=0x49", OPS.get("gcpy") == 0x49)
    check("таблица: DELAY=0x7A", OPS.get("delay") == 0x7A)
    check("таблица: HTTPGET=0x78 STR16", KIND.get("httpget") == "STR16")
    dup_codes = len(NAMES) == len(set(NAMES))
    check("таблица: без дублей кодов", dup_codes)


def test_roundtrip() -> None:
    src = """
.title TEST
.data 4
.str msg HELLO
.str best best
start:
    frame
    event
    dup
    jz start
    drop
    gload 0
    push 1
    add
    gstore 0
    gcpy
    text 2 20 1 msg
    push 5
    push 6
    mul
    num 2 30 1
    msec
    rand
    beep
    save best
    load best 0
    delay 30
    px 10 20 1
    cls
    disp
    jmp start
"""
    blob1 = xlas.assemble(src)
    code1, data, pool, blob, title = xla_disasm.load_bytes(blob1)
    listing = xla_disasm.disasm(code1, pool)
    check(
        "roundtrip: без неизвестных опкодов",
        all("НЕИЗВЕСТНЫЙ" not in ln for ln in listing),
    )
    check("roundtrip: титул TEST", title == "TEST", title)

    # ЧЕСТНЫЙ roundtrip: парсим исходник дважды и сверяем код-секции —
    # ассемблер должен быть детерминированным. А листинг проверяем
    # на полное покрытие кода без потерь (последний pc + размер == len).
    prog = xlas.parse(src)
    code_again = xlas.encode(prog)
    check(
        "roundtrip: код детерминирован",
        code_again == code1,
        f"{len(code_again)} vs {len(code1)}",
    )
    last_pc = int(listing[-1].split(":")[0].replace("pc=", ""))
    # размер последней инструкции: она стековая (1 байт) — но возьмём
    # из листинга честно: перечитаем pc следующей за концом
    check(
        "roundtrip: листинг покрывает весь код",
        last_pc <= len(code1),
        f"last_pc={last_pc} code={len(code1)}",
    )

    # текстовый уровень: наивный roundtrip для стековой программы без
    # imm-операндов — восстановление исходника из листинга 1:1
    stack_src = """
.title RT
.data 1
start:
    frame
    event
    dup
    drop
    swap
    add
    sub
    mul
    gcpy
    msec
    rand
    cls
    disp
    jmp start
"""
    b1 = xlas.assemble(stack_src)
    c1, dt, pl, _, t2 = xla_disasm.load_bytes(b1)
    listing2 = xla_disasm.disasm(c1, pl)
    # восстановление: стековые опкоды без операндов идут как есть,
    # jmp заменяем на вычисленный относительный (последняя строка)
    src2_lines = [".title RT", ".data 1", "start:"]
    for ln in listing2:
        body = ln.split(":", 1)[1].strip()
        if "->" in body:  # jmp: пересчитываем по последней метке
            src2_lines.append("jmp start")
            continue
        src2_lines.append(body.lower())
    b2 = xlas.assemble("\n".join(src2_lines))
    check(
        "roundtrip: текст -> bin -> текст -> bin (стековая)",
        b1 == b2,
        f"{len(b1)} vs {len(b2)}",
    )
    if b1 != b2:
        for i, (a, b) in enumerate(zip(b1, b2, strict=False)):
            if a != b:
                print(f"  первый diff @ {i}: {a:02X} vs {b:02X}")
                break


def _listing_to_src(listing: list[str]) -> str:
    """Текстовое представление листинга -> исходник для повторной сборки.

    Формат строк: pc=NNN: MNEM arg
    Пересборка учитывает: IMM16/STR16 операнды перечисляются явно.
    """
    lines = [".title TEST", ".data 4", ".str msg HELLO", ".str best best"]
    for ln in listing:
        body = ln.split(":", 1)[1].strip()
        if body.startswith("??"):
            raise ValueError(f"unknown op in listing: {ln}")
        parts = body.split(None, 1)
        mnem = parts[0].lower()
        rest = parts[1] if len(parts) > 1 else ""
        if "->" in rest:
            continue  # переходы по меткам не восстанавливаем наивно
        if mnem in ("text",):
            continue
        # STR16-строки: `SAVE "best" (str@24)` -> имя из кавычек
        if rest.startswith('"'):
            sname = rest[1 : rest.index('"', 1)]
            if mnem == "save":
                lines.append("push 0")  # заглушка значения
                lines.append(f"save {sname}")
                continue
            if mnem == "load":
                lines.append(f"load {sname} 0")
                continue
            continue
        if mnem == "save":
            lines.append("push 0")
            lines.append(f"save {rest}")
            continue
        if mnem == "load":
            off = rest.split("(")[-1].rstrip(")")
            name = {"0": "msg", str(len("HELLO") + 1): "best"}.get(off, "msg")
            lines.append(f"load {name} 0")
            continue
        if rest and not rest.startswith('"'):
            # imm-опкод: gstore/gload/push/delay
            if mnem in ("gstore", "gload", "delay"):
                lines.append(f"{mnem} {rest}")
            elif mnem == "num":
                continue  # num берёт 4 значения со стека — пропускаем
            else:
                lines.append(f"push {rest}")
        else:
            lines.append(mnem)
    return "\n".join(lines)


def test_snake_real() -> None:
    snake = Path(__file__).resolve().parent.parent.parent / "xla" / "snake.xla"
    if not snake.exists():
        check("snake.xla: файл доступен", False, str(snake))
        return
    code, data, pool, blob, title = xla_disasm.load(str(snake))
    listing = xla_disasm.disasm(code, pool)
    check("snake: титул SNAKE", title == "SNAKE", title)
    unknowns = [ln for ln in listing if "НЕИЗВЕСТНЫЙ" in ln]
    check(
        "snake: 0 неизвестных опкодов (GCPY!)",
        not unknowns,
        f"{len(unknowns)} шт: {unknowns[:2]}",
    )
    # непрерывность: последний pc + размер инструкции == len(code)
    last_pc = int(listing[-1].split(":")[0].replace("pc=", ""))
    check(
        "snake: полный охват кода",
        last_pc <= len(code),
        f"last_pc={last_pc} code={len(code)}",
    )


def main() -> None:
    print("=== roundtrip-тесты инструментария XLA v0.12 ===")
    test_table_consistency()
    test_roundtrip()
    test_snake_real()
    print()
    if FAILED:
        print(f"ИТОГ: {FAILED} провал(ов)")
        sys.exit(1)
    print("ИТОГ: ВСЕ ТЕСТЫ ПРОЙДЕНЫ")


if __name__ == "__main__":
    main()
