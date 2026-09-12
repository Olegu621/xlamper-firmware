#pragma once
// ==================================================================
//  xla_opcodes.h — ЕДИНСТВЕННЫЙ источник правды по опкодам XLA VM.
//
//  Корневой баг v0.11: таблицы опкодов жили в трёх местах
//  (switch VM / xlas.py / xla_disasm.py) и разошлись — VM не знала
//  0x49 GCPY, 0x78-0x7B HTTP/DELAY, которые генерировал её же
//  ассемблер. Облачный SNAKE падал с "bad op 49" на первом шаге.
//
//  Теперь: одна таблица здесь. VM строит обработчики по ней
//  (пропуск обработчика = ошибка линковки), инструменты
//  (tools/) генерируют свою таблицу из ЭТОГО файла (gen_opcodes.py),
//  roundtrip-тест ловит рассинхрон.
//
//  Вид:  X(код, МНЕМОНИКА, ОПЕРАНД)
//  ОПЕРАНД: NONE  — все аргументы со стека
//           IMM16 — встроенный int16 (индекс/переход/число)
//           STR16 — встроенный индекс строки (смещение в пуле строк)
// ==================================================================

#define XLA_OP_TABLE(X) \
  /* -- стек -- */                                                     \
  X(0x00, HALT,    NONE)   /* стоп программы                       */  \
  X(0x01, PUSH,    IMM16)  /* положить int16                       */  \
  X(0x02, DUP,     NONE)   /* d -- d d                             */  \
  X(0x03, DROP,    NONE)   /* d --                                 */  \
  X(0x04, SWAP,    NONE)   /* a b -- b a                           */  \
  X(0x05, OVER,    NONE)   /* a b -- a b a                         */  \
  X(0x06, PICK,    NONE)   /* n v -- v[n]                          */  \
  /* -- арифметика (int16) -- */                                      \
  X(0x20, ADD,     NONE)   /* a b -- a+b                           */  \
  X(0x21, SUB,     NONE)   /* a b -- a-b                           */  \
  X(0x22, MUL,     NONE)   /* a b -- a*b (32-битный пром.)        */  \
  X(0x23, DIV,     NONE)   /* a b -- a/b (div0 = ошибка VM)        */  \
  X(0x24, MOD,     NONE)   /* a b -- a%b (mod0 = ошибка VM)        */  \
  X(0x25, NEG,     NONE)   /* a -- -a                              */  \
  X(0x26, MIN,     NONE)   /* a b -- min                           */  \
  X(0x27, MAX,     NONE)   /* a b -- max                           */  \
  X(0x28, ABS,     NONE)   /* a -- |a|                             */  \
  /* -- сравнения/логика -- */                                        \
  X(0x30, EQ,      NONE)   /* a b -- a==b                          */  \
  X(0x31, NE,      NONE)   /* a b -- a!=b                          */  \
  X(0x32, LT,      NONE)   /* a b -- a<b                           */  \
  X(0x33, LE,      NONE)   /* a b -- a<=b                          */  \
  X(0x34, GT,      NONE)   /* a b -- a>b                           */  \
  X(0x35, GE,      NONE)   /* a b -- a>=b                          */  \
  X(0x36, AND,     NONE)   /* a b -- (a&&b)                        */  \
  X(0x37, OR,      NONE)   /* a b -- (a||b)                        */  \
  X(0x38, XOR,     NONE)   /* a b -- (a!=b логич.)                 */  \
  X(0x39, NOT,     NONE)   /* a -- !a                              */  \
  /* -- глобальная память (data-секция, int16-слоты) -- */             \
  X(0x45, GSTORE,  IMM16)  /* v idx -- data[idx]=v                 */  \
  X(0x46, GLOAD,   IMM16)  /* idx -- data[idx]                     */  \
  X(0x47, GSTOREI, NONE)   /* v idx(со стека) -- data[idx]=v       */  \
  X(0x48, GLOADI,  NONE)   /* idx(со стека) -- data[idx]           */  \
  X(0x49, GCPY,    NONE)   /* src dst n -- копия n слотов          */  \
  /* -- переходы (rel16 от PC после операнда) -- */                    \
  X(0x50, JMP,     IMM16)                                             \
  X(0x51, JZ,      IMM16)  /* c -- (переход если c==0)             */  \
  X(0x52, JNZ,     IMM16)  /* c -- (переход если c!=0)             */  \
  X(0x53, CALL,    IMM16)                                             \
  X(0x54, RET,     NONE)                                              \
  X(0x5F, FRAME,   NONE)   /* конец кадра (60 fps бюджет)          */  \
  /* -- графика 128x64 -- */                                          \
  X(0x60, PX,      NONE)   /* x y c -- точка                       */  \
  X(0x61, LINE,    NONE)   /* x0 y0 x1 y1 c -- линия               */  \
  X(0x62, RECT,    NONE)   /* x y w h c -- рамка                  */  \
  X(0x63, FRECT,   NONE)   /* x y w h c -- заливка                 */  \
  X(0x64, CIRC,    NONE)   /* x y r c -- окружность                */  \
  X(0x65, FCIRC,   NONE)   /* x y r c -- круг                      */  \
  X(0x66, ELL,     NONE)   /* x y rx ry c -- эллипс               */  \
  X(0x67, TEXT,    STR16)  /* x y size str -- текст               */  \
  X(0x68, INV,     NONE)   /* инверсия-вспышка экрана              */  \
  X(0x69, FILL,    NONE)   /* c -- залить экран                    */  \
  X(0x6A, CLS,     NONE)   /* очистить буфер кадра                 */  \
  X(0x6B, DISP,    NONE)   /* вывести буфер на экран               */  \
  /* -- система -- */                                                  \
  X(0x70, MSEC,    NONE)   /* -- мс (uint16)                        */  \
  X(0x71, RAND,    NONE)   /* m -- случайное 0..m-1               */  \
  X(0x72, BEEP,    NONE)   /* f ms -- звук                          */  \
  X(0x73, EXIT,    NONE)   /* выход в меню                          */  \
  X(0x74, SAVE,    STR16)  /* v -- рекорд в NVS (xla:TITLE_key)    */  \
  X(0x75, LOAD,    STR16)  /* def -- рекорд из NVS                 */  \
  X(0x76, LOG,     NONE)   /* v -- печать в Serial                  */  \
  X(0x77, NUM,     NONE)   /* x y size v -- число на экран          */  \
  X(0x78, HTTPGET, STR16)  /* url(строка) -- длина ответа | -1     */  \
  X(0x79, HTTPCH,  NONE)   /* i -- байт i последнего ответа (0 OOB)*/  \
  X(0x7A, DELAY,   NONE)   /* ms -- пауза (кап 200 мс)             */  \
  X(0x7B, WGET,    NONE)   /* url(dst-строка) dst cnt -- слов      */  \
  /* -- ввод -- */                                                     \
  X(0x80, STX,     NONE)   /* -- стик X (0..4095)                  */  \
  X(0x81, STY,     NONE)   /* -- стик Y (0..4095)                  */  \
  X(0x82, STICK,   NONE)   /* -- 8-way: -1 покой 0..7              */  \
  X(0x83, EVENT,   NONE)   /* -- событие ввода кадра               */  \
  X(0x84, HOLD,    NONE)   /* -- прогресс удержания кнопки 0..100  */  \
  /* -- математика (fixed x1000) -- */                                \
  X(0x90, SIN,     NONE)   /* град -- sin*1000                     */  \
  X(0x91, COS,     NONE)   /* град -- cos*1000                     */  \
  X(0x92, SQRT,    NONE)   /* v -- sqrt(v)                         */

// Количество опкодов (для самопроверок)
#define XLA_OP_COUNT_IMPL(OP, MNEM, KIND) +1
constexpr int XLA_OP_COUNT = 0 XLA_OP_TABLE(XLA_OP_COUNT_IMPL);

// Имя опкода по коду (для диагностики "bad op GCPY 49")
#define XLA_OP_NAME_IMPL(OP, MNEM, KIND) case OP: return #MNEM;
inline const char* xlaOpName(uint8_t op) {
  switch (op) { XLA_OP_TABLE(XLA_OP_NAME_IMPL) default: return "?"; }
}
