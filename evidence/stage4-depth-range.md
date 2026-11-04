# Stage 4: найден корень чёрного кадра — гостевой depth range 0..0

## Что измерено (устройство, сборка 0046/0047)

Зонд `NVN_DRAW_DEPTHSTATE` на каждом из первых дровов гостя:

```
NVN_DRAW_DEPTHSTATE number=1 range=0..0 mask=0
NVN_PROBE_QUAD number=1 target=0x3a5113af8 fbo=2 attach=72 match=1 stencil=0 ref=0 mask=0xffffffff error=0x0
NVN_DRAW_PIXELS number=1 target=0x3a5113af8 complete=0x8cd5 max=255 px=255,0,255,255 readerr=0x0
```

`range=0..0` — гость оставил вырожденный `GL_DEPTH_RANGE`: все фрагменты получают z = 0 при любом диапазоне
window-space, и Mali-G57 отбрасывает их даже при выключенном depth-тесте (проверено: `GL_DEPTH_WRITEMASK`,
`GL_STENCIL_TEST`, `GL_RASTERIZER_DISCARD`, `colorMask`, viewport — все в норме; FBO COMPLETE 0x8cd5;
отсоединение depth-RB мгновенно возвращает пиксели).

## Фикс (сборка 0047)

Перед каждым дровами гостя вырожденный диапазон заменяется на 0..1:

```c
if(!depth_draw.active){GLfloat dr[2];glGetFloatv(GL_DEPTH_RANGE,dr);if(dr[0]==dr[1])glDepthRangef(0.0f,1.0f);}
```

`zero_depth_draw` (осознанная оптимизация «рисовать в постоянную глубину») не затрагивается.

## Результат (сборка 0047/0048, прогон 11:39–11:41)

- `NVN_PRESENT_PIXELS frame=1 nonblack=921600 max=255 status=PIXELS` — кадр целиком не чёрный (было: 0 из 921600).
- `NVN_PRESENT_SOURCE frame=1 max=255` — текстура, которую сэмплит present-квад, содержит пиксели.
- `NVN_DRAW_PIXELS` на целях с depth: `max=255 px=...`, ранее всегда `max=0 px=0,0,0,0`.
- Игра доходит до 339–482 кадров, 160 дровов за кадр (ранее стоп на 120 чёрных кадрах).

## Оставшиеся блокеры

1. `NVN_GL_ERROR op=nvnCommandBufferSetRenderTargets error=0x502` — 675 раз за прогон, первый шаг внутри
   записи, до `nvn_rt_storage_target`. `NVN_RT_STEP step=storage error=0x502` (зонд снимает ошибку, поэтому
   прогон продолжается).
2. `STOP status=CPU_FAULT pc=0x7fb56fe0d0 detail=0x59e1` после ~340 кадров, `exit=22` — гостевое падение.
3. Кадр «не чёрный», но `nonblack=20480` из 921600 (полоса 1280×16) в большинстве кадров — картинка
   неполная.
4. Внешняя съёмка: `kmsgrab` работает только когда порт жив; после падения на экране часы SpruceOS.

## Correction (build 0055)

`NX_PRESENT_CLEAR`, `NX_PROBE_QUAD`, `NX_PROBE_NO_DEPTH` проверялись через `getenv(...)`, то есть
значение `"0"` из лаунчера считалось включённым флагом. Поэтому «нечёрный кадр» и магентовый PPM
(`nvn-frame-ppm-*`, весь кадр FF00FF) были нашей собственной заливкой, а не картинкой игры.

Введён `nvn_flag_on()` (пусто/0/false = выключено), флаги переведены на него.

После исправления (build 0055, 278 кадров): `NVN_PRESENT_PIXELS nonblack=0 max=0 status=BLACK-SAMPLE`,
`NVN_PRESENT_SOURCE max=0`, `NVN_DRAW_PIXELS max=0` на целях 0xc656990 / 0x3a5113af8 / 0x3a517bf98.
Картинки пока нет; глубина-фикс остаётся в силе, но гостевые дровы дают чёрный.

## Stage 5 (builds 0056–0059): гостевые дровы не доходят до растеризации

- `NVN_DRAW_OCCL` (`GL_ANY_SAMPLES_PASSED` вокруг каждого гостевого дрова, первые 8): `samples=0` во всех.
  Значит фрагменты не появляются вовсе — дело не в цвете/записи, а в геометрии.
- `draw-buffer0=0x8ce0` (COLOR_ATTACHMENT0) — вывод не отключён.
- `NVN_DRAW_STATE2`: `cull=0 scissor=0 box=0,0,16384,16384 blend=0 depthtest=0` — состояние уже чистое.
- `NX_FORCE_STATE=1` (принудительно cull/scissor/stencil/blend/depth off, colormask on, viewport 0,0,1280,720): `samples` всё равно 0.
- `NX_NO_DEPTH_TEST=1`: без изменений.
- Найдена и исправлена реальная ошибка: cbuf-семплеры в **вершинной** стадии получали unit 127
  (`GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS` на Mali отдаёт 128, реально вершинных юнитов 32).
  Теперь вершинные cbuf — unit 31/30 (`unit = min(limit,32)-1-binding`). На `samples` это не повлияло.
- Гость чистит цель в чёрный (`MOMO_CLEAR_COLOR target=130 rgba=0,0,0,0`) и рисует 6 дровов на кадр.
- Итог: программы/состояние валидны, но вершины после VS вырождены или вне clip-space.

Следующий прибор: transform feedback вокруг одного гостевого дрова — прочитать фактические `gl_Position`
6 вершин и сравнить с ожидаемым clip-space.
