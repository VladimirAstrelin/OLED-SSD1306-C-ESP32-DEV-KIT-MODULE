/* main.cpp
   Interactive demo: invert/frame/table modes controlled by a button.
   ESP32 DevKit + SSD1306 128x32 (I2C SDA=21, SCL=22). Button -> GPIO4 to GND.
*/

#include <Arduino.h>            // Базовая Arduino-платформа: функции setup(), loop(), delay(), Serial и т.д.
#include <Wire.h>               // Библиотека для I2C (Wire) — используется для общения с OLED по I2C.
#include <Adafruit_GFX.h>       // Adafruit GFX — базовая графическая библиотека (рисование линий, текста, шрифтов).
#include <Adafruit_SSD1306.h>   // Драйвер SSD1306 от Adafruit, реализует интерфейс дисплея (буфер, команды и пр).

// --- Параметры дисплея и I2C ---
// Размеры дисплея в пикселях — важны для расчёта позиционирования и центрации
#define SCREEN_WIDTH 128       // Ширина экрана в пикселях
#define SCREEN_HEIGHT 32       // Высота экрана в пикселях

#define OLED_RESET -1          // Пин RESET дисплея. -1 означает, что аппаратный RST не используется.
#define OLED_ADDR 0x3C         // I2C-адрес дисплея (часто 0x3C или 0x3D — у вас 0x3C)
#define SDA_PIN 21             // GPIO для SDA (I2C data) — у вас подключено на 21
#define SCL_PIN 22             // GPIO для SCL (I2C clock) — у вас подключено на 22

// --- Настройка кнопки ---
// Пин, к которому подключена кнопка. Рекомендовался GPIO4 — свободный, безопасный при загрузке.
#define BUTTON_PIN 4           // Физический GPIO, на который повешена кнопка (второй контакт кнопки — на GND)
#define DEBOUNCE_MS 50         // Время дебаунса (в мс) — игнорируем дребезг контактов в течение этого времени

// Создаём объект дисплея: передаём ширину, высоту, ссылку на Wire (I2C) и пин RESET
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Режимы работы ---
// Перечисление режимов: инверсия, рамка, таблица — так проще переключать и читать код
enum Mode { MODE_INVERT = 0, MODE_FRAME = 1, MODE_TABLE = 2 };
Mode currentMode = MODE_INVERT; // Текущий активный режим; по умолчанию — MODE_INVERT

// --- Переменные для обработки кнопки ---
// Для корректной обработки с программным дебаунсом нам нужно сохранять последнее состояние кнопки и время изменения
uint32_t lastButtonChange = 0;   // Время (millis) последнего изменения состояния кнопки
bool lastButtonState = HIGH;     // Последнее прочитанное состояние линии кнопки (INPUT_PULLUP -> не нажата = HIGH)
bool buttonFired = false;        // Флаг: событие "нажатие" уже зарегистрировано (чтобы одно удержание не порождало много переключений)

// --- Переменные для режима INVERT (мигание инверсией) ---
uint32_t lastInvertToggle = 0;   // Время последнего переключения состояния инверсии
uint32_t invertInterval = 400;   // Интервал мигания в мс (400ms)
bool invertState = false;        // Текущий логический статус инверсии (true - инвертирован, false - нормальный)

// --- Текст для режима FRAME ---
const char *frameText = "Hello!"; // Строка, вокруг которой рисуется рамка в режиме MODE_FRAME

// --- Прототипы функций (чтобы структура кода была понятнее) ---
void enterMode(Mode m);                  // Вызывается при входе в режим: рисует экран и сбрасывает сопутствующее состояние
void handleButton();                     // Обрабатывает чтение кнопки + дебаунс + переключение режимов
void drawFrameText(const char *text);    // Рисует заданный текст по центру с рамкой
void drawTable();                        // Рисует простую таблицу (заголовок + 2 строки данных)

// --- setup() выполняется один раз при старте платы ---
void setup() {
  Serial.begin(115200);             // Инициализируем последовательный порт для отладки (115200 бод)
  delay(10);                        // Небольшая пауза, чтобы Serial успел инициализироваться
  Serial.println("SSD1306 interactive demo (no overlay labels)");

  // Инициализация I2C: SDA и SCL указываем явно (Wire.begin(sda, scl) на ESP32)
  Wire.begin(SDA_PIN, SCL_PIN);

  // Инициализация дисплея: begin возвращает true при успешной инициализации
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    // Если дисплей не отвечает — сообщаем в Serial и заходим в вечный цикл (без дисплея дальше нет смысла)
    Serial.println(F("SSD1306 init failed"));
    for (;;); // Блокируем выполнение (можно здесь добавить blink светодиода и т.п.)
  }

  // Настройка кнопки: используем внутренний подтягивающий резистор (INPUT_PULLUP)
  // Подключение кнопки: одна нога на GPIO4, другая на GND -> при нажатии сигнальная линия будет LOW.
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Инициализируем переменные для начального режима
  currentMode = MODE_INVERT;        // Устанавливаем стартовый режим
  invertState = false;              // Сбрасываем инверсию
  lastInvertToggle = millis();      // Ставим текущее время для управления таймером мигания

  display.clearDisplay();           // Очищаем буфер дисплея (в памяти MCU)
  enterMode(currentMode);           // Запускаем начальный режим — рисует стартовый экран
}

// --- loop() выполняется бесконечно ---
// Основная логика: читаем кнопку, а если режим INVERT — выполняем неблокирующее мигание инверсией
void loop() {
  handleButton(); // Всегда проверяем кнопку — это даёт интерактивность

  // Если текущий режим — инверсия, периодически переключаем display.invertDisplay()
  if (currentMode == MODE_INVERT) {
    uint32_t now = millis();               // Берём текущее время миллисекундах
    if (now - lastInvertToggle >= invertInterval) { // Если прошло достаточно времени
      lastInvertToggle = now;              // Обновляем отметку времени
      invertState = !invertState;          // Меняем логический флаг состояния инверсии
      display.invertDisplay(invertState);  // Включаем/выключаем аппаратную инверсию экрана
      // Примечание: invertDisplay меняет пиксели на противоположные (0->1, 1->0) аппаратно.
    }
  }
  // Небольшая пауза, чтобы loop не крутился слишком быстро (и CPU не был загружен)
  delay(10);
}

// --- handleButton() ---
// Читает состояние кнопки, реализует дебаунс и переключает режимы по нажатию.
// Алгоритм дебаунса: фиксируем время изменения состояния, после стабилизации >DEBOUNCE_MS
// считаем, что событие состоялось. При удержании флаг buttonFired предотвращает множественные срабатывания.
void handleButton() {
  bool reading = digitalRead(BUTTON_PIN); // Считываем текущее состояние кнопки (HIGH = не нажата, LOW = нажата)
  uint32_t now = millis();                // Текущее время для дебаунса

  // Если состояние изменилось — обновляем время изменения и запоминаем новое состояние
  if (reading != lastButtonState) {
    lastButtonChange = now;               // Отметка времени изменения
    lastButtonState = reading;            // Обновляем последнее состояние (для последующих проверок)
  }
  // Если состояние стабильно дольше, чем DEBOUNCE_MS — обрабатываем его как "реальное"
  else if ((now - lastButtonChange) > DEBOUNCE_MS) {
    // Если текущее стабильное состояние — LOW (нажата) и ранее мы ещё не регистрировали это нажатие
    if (reading == LOW && !buttonFired) {
      buttonFired = true;                 // Помечаем, что событие нажатия уже обработано (чтобы удержание не давало множественных переключений)
      // Переключаем режим циклически: (0->1->2->0)
      currentMode = static_cast<Mode>((currentMode + 1) % 3);
      Serial.printf("Switching to mode %d\n", currentMode); // Лог в Serial для отладки
      enterMode(currentMode);             // Вызываем обработчик входа в новый режим (перерисовка экрана)
    }
    // Если кнопка отпущена (HIGH) и ранее был флаг buttonFired, снимаем флаг — разрешаем новое нажатие
    else if (reading == HIGH && buttonFired) {
      buttonFired = false;                // Сброс флага — готовность к следующему нажатию
    }
  }
}

// --- enterMode(Mode m) ---
// Вызывается при смене режима. Сбрасывает инверсию и рисует соответствующее содержимое для выбранного режима.
void enterMode(Mode m) {
  display.invertDisplay(false); // Всегда выключаем инверсию при смене режима — чтобы начать "чисто"
  invertState = false;          // Синхронизируем флаг состояния инверсии

  switch (m) {
    case MODE_INVERT: {
      // Режим: показываем слово "Invert" по центру и далее цикл в loop будет управлять миганием
      display.clearDisplay();          // Очищаем буфер
      display.setTextWrap(false);      // Отключаем автоматический перенос текста
      display.setTextSize(2);          // Размер шрифта: 2 (масштаб стандартного bitmap-шрифта)
      display.setTextColor(SSD1306_WHITE); // Цвет текста — белый (на чёрном фоне)

      // Для корректной центрировки узнаём реальные пиксельные габариты текста через getTextBounds
      const char *msg = "Invert";      // Текст, который хотим показать
      int16_t x0, y0;                  // переменные для базовой позиции, которые getTextBounds отдаёт (интерфейс требует их)
      uint16_t w, h;                   // ширина и высота текста в пикселях
      display.getTextBounds(msg, 0, 0, &x0, &y0, &w, &h); // Получаем размер текста (w,h)

      // Центрируем текст: вычисляем координаты (x,y), такие, что текст будет посередине экрана
      int16_t x = (SCREEN_WIDTH - w) / 2;
      int16_t y = (SCREEN_HEIGHT - h) / 2;

      // Устанавливаем курсор и выводим текст в буфер
      display.setCursor(x, y);
      display.println(msg);

      // Отправляем буфер на дисплей — теперь слово "Invert" видно на экране
      display.display();

      // Сбрасываем таймер и флаг инверсии, чтобы мигание началось с предсказуемого состояния
      lastInvertToggle = millis();
      invertState = false;
      break;
    }

    case MODE_FRAME:
      // Рисуем текст с рамкой (функция рассчитывает центрирование и отрисовывает прямоугольник вокруг текста)
      drawFrameText(frameText);
      break;

    case MODE_TABLE:
      // Рисуем простую таблицу с заголовком и 2 строками данных
      drawTable();
      break;
  }
}

// --- drawFrameText ---
// Центрирует переданный текст и рисует прямоугольную рамку с отступом (padding).
// getTextBounds используется для определения реального пиксельного размера строки
void drawFrameText(const char *text) {
  display.clearDisplay();           // Очищаем буфер перед рисованием
  display.setTextWrap(false);       // Выключаем перенос
  display.setTextSize(2);           // Делаем текст крупным (scale 2)
  display.setTextColor(SSD1306_WHITE);

  // Узнаём пиксельные габариты текста (w,h)
  int16_t x0, y0;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x0, &y0, &w, &h);

  // Центруем текст по экрану
  int16_t x = (SCREEN_WIDTH - w) / 2;
  int16_t y = (SCREEN_HEIGHT - h) / 2;

  // Ставим курсор и печатаем текст в буфер
  display.setCursor(x, y);
  display.println(text);

  // Параметры рамки: padding = отступ между текстом и рамкой
  const uint8_t pad = 4;
  int16_t rx = x - pad;                 // координата X левого верхнего угла рамки
  int16_t ry = y - pad;                 // координата Y левого верхнего угла рамки
  int16_t rw = w + pad * 2;             // ширина рамки
  int16_t rh = h + pad * 2;             // высота рамки

  // Обрезаем рамку, если она выходит за пределы экрана (безопасность)
  if (rx < 0) { rw += rx; rx = 0; }     // если rx отрицателен, уменьшаем ширину и ставим rx=0
  if (ry < 0) { rh += ry; ry = 0; }     // аналогично по вертикали
  if (rx + rw > SCREEN_WIDTH) rw = SCREEN_WIDTH - rx;   // не выходим за правую границу
  if (ry + rh > SCREEN_HEIGHT) rh = SCREEN_HEIGHT - ry; // не выходим за нижнюю границу

  // Рисуем рамку (не заполненную)
  display.drawRect(rx, ry, rw, rh, SSD1306_WHITE);

  // Обновляем дисплей — теперь видно текст и рамку
  display.display();
}

// --- drawTable ---
// Рисует простую табличную раскладку (рамка, горизонтальная линия раздела заголовка, вертикальные линии колонок),
// затем печатает заголовки колонок и 2 строки данных.
void drawTable() {
  display.clearDisplay();           // Очищаем буфер
  display.setTextWrap(false);       // Отключаем перенос
  display.setTextSize(1);           // Малый шрифт для таблицы (вмещается в 32px высоты)
  display.setTextColor(SSD1306_WHITE);

  // Параметры таблицы — задаём расположение и размеры
  const int left = 0;
  const int top = 0;
  const int tableW = SCREEN_WIDTH;
  const int tableH = SCREEN_HEIGHT;
  const int headerH = 10;           // Высота области заголовка в пикселях
  const int rowH = 10;              // Высота одной строки данных
  const int rows = 2;               // Количество строк данных (в 32px точно помещается 2 строки + заголовок)

  // Колонки: подбираем их вручную под 128px ширины
  const int col1 = 40;              // правая граница первой колонки (ID)
  const int col2 = 86;              // правая граница второй колонки (Name)
  const int col3 = SCREEN_WIDTH;    // правая граница третьей колонки (Val)

  // Внешняя рамка таблицы
  display.drawRect(left, top, tableW, tableH, SSD1306_WHITE);

  // Горизонтальная линия, отделяющая заголовок от данных
  display.drawLine(left, top + headerH, left + tableW - 1, top + headerH, SSD1306_WHITE);

  // Вертикальные линии колонок
  display.drawLine(col1, top, col1, top + tableH - 1, SSD1306_WHITE);
  display.drawLine(col2, top, col2, top + tableH - 1, SSD1306_WHITE);

  // Заголовки колонок: ставим курсоры и печатаем
  display.setCursor(4, 2);          // небольшие отступы от левого края
  display.println(F("ID"));
  display.setCursor(col1 + 4, 2);
  display.println(F("Name"));
  display.setCursor(col2 + 4, 2);
  display.println(F("Val"));

  // Данные для демонстрации (2 строки)
  const char *names[] = {"Temp", "Hum"};   // Названия параметров
  const char *vals[]  = {"23.4C", "54%"};  // Значения параметров

  // Итерируем по строкам и печатаем ID, Name, Value
  for (int r = 0; r < rows; ++r) {
    int y = headerH + r * rowH + 2;  // Y-координата для курсора в этой строке (+2 — небольшой вертикальный отступ)
    // ID (число строки)
    display.setCursor(4, y);
    display.printf("%d", r + 1);

    // Name (лево-ориентированно)
    display.setCursor(col1 + 4, y);
    display.println(names[r]);

    // Value нужно правое выравнивание в последней колонке.
    // Для этого определяем ширину строки значения и вычисляем X координату так, чтобы правый край оказался в col3-4.
    int16_t x0, y0; uint16_t w, h;
    display.getTextBounds(vals[r], 0, 0, &x0, &y0, &w, &h); // w — ширина значения в пикселях
    int16_t vx = col3 - w - 4;          // X для курсора так, чтобы правый отступ был 4px
    display.setCursor(vx, y);
    display.println(vals[r]);
  }

  // Обновляем экран одним display() — данные видны пользователю
  display.display();
}