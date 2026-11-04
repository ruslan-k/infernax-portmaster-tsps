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
