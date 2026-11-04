## (б) Три следующих обратимых теста

Их можно реализовать как три диагностических режима одной сборки. Каждый тест должен восстанавливать исходные GL bindings и state после завершения.

### 1. Принудительный color clear при подключённом depth

<badge color="success">Приоритет №1</badge>

**Цель:** установить, пропадают ли только фрагменты или даже операции очистки цветового буфера.

На проблемном FBO 1280×720, не отсоединяя depth, выполнить:

```c
glBindFramebuffer(GL_FRAMEBUFFER, fbo);

const GLenum bufs[] = { GL_COLOR_ATTACHMENT0 };
glDrawBuffers(1, bufs);
glReadBuffer(GL_COLOR_ATTACHMENT0);

glDisable(GL_SCISSOR_TEST);
glDisable(GL_RASTERIZER_DISCARD);
glDisable(GL_DEPTH_TEST);
glDisable(GL_STENCIL_TEST);

glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
glClear(GL_COLOR_BUFFER_BIT);

glFinish();

uint8_t px[4] = {0};
glReadPixels(640, 360, 1, 1,
             GL_RGBA, GL_UNSIGNED_BYTE, px);
```

Дополнительно логировать `GL_DRAW_FRAMEBUFFER_BINDING`, `GL_READ_FRAMEBUFFER_BINDING`, `GL_DRAW_BUFFER0`, `GL_READ_BUFFER` до принудительной установки, а также ошибки непосредственно после clear, finish и read. Перед тестом очистить накопленные GL errors.

| Результат | Интерпретация |
|---|---|
| `0,255,0,255` | Color attachment работает даже с depth. Проблему искать в draw pipeline |
| `0,0,0,0` при `COMPLETE` и отсутствии GL errors | Подозрение на attachment storage, GPU fault или driver/render-pass failure |
| Зелёный только после принудительной установки bindings/buffers | Ошибка управления GL state в мосте |
| `0x502` после конкретной команды | Найдена точка возникновения API-ошибки; расследовать её до изменения шейдеров |

Затем повторить **точно ту же последовательность** после отсоединения depth, не меняя остальных состояний.

### 2. Матрица форматов depth renderbuffer

<badge>Приоритет №2</badge>

**Цель:** проверить, специфична ли проблема для D24S8, его способа подключения или вообще любого depth attachment.

Оставить цветовую RGBA8-текстуру и разрешение 1280×720 без изменений. Создавать отдельные renderbuffer для каждого варианта.

| Конфигурация | Ожидаемый color clear |
|---|---|
| Нет depth | Зелёный — контроль |
| `GL_DEPTH_COMPONENT16` → `GL_DEPTH_ATTACHMENT` | Зелёный |
| `GL_DEPTH_COMPONENT24` → `GL_DEPTH_ATTACHMENT` | Зелёный |
| `GL_DEPTH24_STENCIL8` → `GL_DEPTH_ATTACHMENT` | Зелёный |
| `GL_DEPTH24_STENCIL8` → `GL_DEPTH_STENCIL_ATTACHMENT` | Зелёный |

Для всех конфигураций также проверить контрольный квад.

Обрати внимание: `GL_DEPTH_STENCIL_ATTACHMENT` подключает renderbuffer одновременно к depth и stencil. Это корректный путь для packed D24S8. Подключение packed-формата только к depth также допустимо в GLES 3.x, поэтому его отказ при `COMPLETE` не следует считать нормальным поведением API. <Cite refs={["turn108458search10","turn108458search14"]}/>

**Интерпретация:**

- Работает D16/D24, не работает D24S8 → проблема packed depth/stencil или его обработки драйвером.
- D24S8 работает только при подключении к обоим attachment → сильное свидетельство проблемы depth-only конфигурации packed renderbuffer.
- Все depth-варианты чёрные → проблема не в конкретном формате; искать render-pass, state или GPU fault.
- Clear работает во всех вариантах, а квад не работает → изолировать rasterization/fragment pipeline.

### 3. Новый FBO до и после guest replay + уменьшение размера

<badge>Приоритет №3</badge>

**Цель:** разделить проблему самого драйвера и проблему состояния или ресурсов, оставленных NVN→GLES мостом.

Создать отдельную диагностическую пару RGBA8 + D24S8 с идентичным простым clear и quad:

- Вариант A: новый FBO 256×256 до запуска guest draw commands.
- Вариант B: новый FBO 1280×720 до guest replay.
- Вариант C: новый FBO 1280×720 после первых гостевых draw commands, когда исходная проблема уже проявилась.

В каждом варианте использовать полностью заданные bindings и состояние из теста №1, `glFinish()` и readback. Не использовать guest texture/renderbuffer objects.

| Результат | Вывод |
|---|---|
| Все три работают | Дефект в конкретном guest FBO или его жизненном цикле |
| A/B работают, C нет | Зависимость от guest replay: ресурсное давление, driver fault, повреждённое состояние |
| A работает, B/C нет | Зависимость от размера или объёма GPU-ресурсов |
| Ни один не работает, даже в свежем EGL-контексте | Сильное подозрение на driver/format issue |

Этот тест особенно важен из-за `CPU_FAULT`. На Mali данные могут оставаться в tile-local storage до завершения соответствующего этапа рендеринга; исправность FBO на стороне API не доказывает, что GPU успешно завершил обработку и запись тайлов. <Cite ref="turn156888view0"/>

Если тест C вызывает fault, важно записать первый fault и первый GL error, а не диагностировать только итоговое чёрное изображение.

---

## (в) Какие состояния GLES способны давать чёрный FBO без ошибки

| Состояние | Эффект | Может влиять на `glClear`? |
|---|---|---|
| `GL_SCISSOR_TEST` + пустая/неверная scissor box | Отсекает все пиксели | Да |
| `GL_DRAW_BUFFER0 = GL_NONE` | Не записывает fragment color | Да |
| Неправильный `GL_READ_FRAMEBUFFER` / `GL_READ_BUFFER` | Читает другую цель | Да, на наблюдаемый результат |
| `GL_COLOR_WRITEMASK` выключен | Блокирует запись цвета | Да |
| `GL_RASTERIZER_DISCARD` | Отбрасывает примитивы | Да, игнорирует clear |
| `GL_DEPTH_TEST` + `GL_NEVER` или неудачный Z-test | Отбрасывает фрагменты | Нет |
| `GL_STENCIL_TEST` + `GL_NEVER` | Отбрасывает фрагменты | Нет |
| `GL_SAMPLE_COVERAGE`, `GL_SAMPLE_MASK` | Может отключить все samples при MSAA | Нет |
| `GL_SAMPLE_ALPHA_TO_COVERAGE` | Может убрать все covered samples при нулевой alpha | Нет |
| `GL_CULL_FACE`, winding | Отбрасывает треугольники | Нет |
| Неверные clip coordinates, `gl_Position` | Примитивы не проходят clipping | Нет |
| `discard` в fragment shader | Отбрасывает фрагменты | Нет |
| Shader output не соответствует draw-buffer location | Цвет не попадает в нужный attachment | Нет |

`GL_SAMPLE_MASK` относится к GLES 3.1+, а sample coverage влияет на multisample rendering. В GLES 3.2 эти проверки доступны, но при односэмпловом FBO малоперспективны. <Cite refs={["turn956173search3","turn956173search2","turn108458search2"]}/>

Отдельно проверить наличие `glInvalidateFramebuffer`/`glDiscardFramebufferEXT` для цветового attachment между draw и readback. Они могут сделать его содержимое неопределённым. Нулевой результат в таком случае не означает, что фрагменты были отброшены.

**Не являются самостоятельным объяснением исчезновения color clear:** `GL_DEPTH_RANGE`, `GL_DEPTH_WRITEMASK`, `GL_DEPTH_FUNC`, `GL_BLEND`, `GL_STENCIL_FUNC`.

## Итоговый порядок принятия решения

<box border radius="lg" padding={3} gap={2}>
  <box align="center" gap={1}>
    **Тест №1: color clear с подключённым depth**
    <text color="secondary" size="xs">Принудительно правильные READ/DRAW bindings и masks</text>
  </box>
  <svg viewBox="0 0 320 126" width="100%" xmlns="http://www.w3.org/2000/svg">
    <rect x="8" y="7" width="142" height="46" rx="7" fill="#166534" fillOpacity="0.10" stroke="#16a34a"/>
    <rect x="170" y="7" width="142" height="46" rx="7" fill="#b45309" fillOpacity="0.10" stroke="#d97706"/>
    <g fontSize="11" textAnchor="middle" fill="currentColor">
      <text x="79" y="26" fontWeight="600">Зелёный</text>
      <text x="79" y="41">Color storage OK</text>
      <text x="241" y="26" fontWeight="600">Чёрный</text>
      <text x="241" y="41">Clear/storage failure</text>
    </g>
    <path d="M79 53 V74 M241 53 V74" stroke="currentColor" opacity=".55" strokeWidth="1.4"/>
    <path d="M75 70 L79 76 L83 70 M237 70 L241 76 L245 70" fill="none" stroke="currentColor" opacity=".55"/>
    <rect x="8" y="78" width="142" height="40" rx="7" fill="currentColor" fillOpacity=".05" stroke="currentColor" strokeOpacity=".22"/>
    <rect x="170" y="78" width="142" height="40" rx="7" fill="currentColor" fillOpacity=".05" stroke="currentColor" strokeOpacity=".22"/>
    <g fontSize="11" textAnchor="middle" fill="currentColor">
      <text x="79" y="94" fontWeight="600">Проверить draw</text>
      <text x="79" y="108">shader, samples, culling</text>
      <text x="241" y="94" fontWeight="600">Тесты №2 и №3</text>
      <text x="241" y="108">формат / GPU fault</text>
    </g>
  </svg>
</box>

**Рекомендация:** не менять пока shader lowering, cbuf или логику depth testing. Сначала получить однозначный результат `glClear(GL_COLOR_BUFFER_BIT)` с подключённым D24S8 и принудительно исправленным состоянием. Если даже этот clear не записывает зелёный цвет, а идентичная операция без depth работает, расследование стоит сосредоточить на настройке attachment, корректности выполнения GPU-команд и жизненном цикле renderbuffer.

Драйверный дефект Mali — рабочая гипотеза, но не установленная причина.
