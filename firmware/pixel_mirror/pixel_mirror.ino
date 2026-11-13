#include <M5Unified.h>
#include <math.h>

// Atom Matrix: one button under the 5x5 LEDs, MPU6886 on SDA 25 / SCL 21.
// The button steps the program. Hold it to reset.
// The browser sends "pm px" and then 192 bytes, a 16x12 RGB332 picture.
// Five-by-five of that picture is shown on the LED matrix, kept dim.

static const int COLS = 16;
static const int ROWS = 12;
static const int PX_N = COLS * ROWS;
static const int LED_N = 25;

static const int MODE_N = 7;
static const char *MODES[] = {
  "ascii dots", "color blocks", "edge glow", "mirror kaleido",
  "lean pan", "horizon", "wedges"
};

static int mode = 0;
static int grid = 14;
static int thresh = 128;
static int hue = 0;
static int wob = 0;
static int ang = 0;
static int mir = 1;
static int inv = 0;

static bool holdA = false;

static char lineBuf[80];
static uint8_t lineLen = 0;
static bool readingPx = false;
static int pxGot = 0;
static uint32_t pxDeadline = 0;
static uint8_t frame[PX_N];
static bool frameValid = false;
static bool paintPending = false;

static uint32_t drewAt = 0;
static uint16_t drewCount = 0;

static void emitAll() {
  Serial.printf("pm set mode %d\n", mode);
  Serial.printf("pm set grid %d\n", grid);
  Serial.printf("pm set thresh %d\n", thresh);
  Serial.printf("pm set hue %d\n", hue);
  Serial.printf("pm set wob %d\n", wob);
  Serial.printf("pm set ang %d\n", ang);
  Serial.printf("pm set mir %d\n", mir);
  Serial.printf("pm set inv %d\n", inv);
}

static void showLeds() {
  // Brightness is squared inside M5Unified. 64 stays near the safe Atom level.
  for (int row = 0; row < 5; row++) {
    int y0 = row * ROWS / 5;
    int y1 = (row + 1) * ROWS / 5;
    if (y1 <= y0) y1 = y0 + 1;
    for (int col = 0; col < 5; col++) {
      int x0 = col * COLS / 5;
      int x1 = (col + 1) * COLS / 5;
      if (x1 <= x0) x1 = x0 + 1;
      int r = 0, g = 0, b = 0, n = 0;
      if (frameValid) {
        for (int y = y0; y < y1; y++) {
          for (int x = x0; x < x1; x++) {
            uint8_t c = frame[y * COLS + x];
            r += (c >> 5) & 7;
            g += (c >> 2) & 7;
            b += c & 3;
            n++;
          }
        }
      }
      uint8_t rr = 0, gg = 0, bb = 0;
      if (n) {
        rr = (uint8_t)(r * 255 / (n * 7));
        gg = (uint8_t)(g * 255 / (n * 7));
        bb = (uint8_t)(b * 255 / (n * 3));
      } else if (row == 0 && col == 0) {
        gg = 40;
      }
      M5.Led.setColor((size_t)(row * 5 + col), rr, gg, bb);
    }
  }
  M5.Led.display();
  drewCount++;
}

static void setMode(int m) {
  mode = (m % MODE_N + MODE_N) % MODE_N;
  Serial.printf("pm set mode %d\n", mode);
  (void)MODES[mode];
  showLeds();
}

static void setGrid(int g) {
  g = constrain(g, 4, 48);
  if (g == grid) return;
  grid = g;
  Serial.printf("pm set grid %d\n", grid);
}

static bool assignKey(const char *key, int val) {
  if (!strcmp(key, "mode")) {
    val = constrain(val, 0, MODE_N - 1);
    if (val == mode) return false;
    mode = val;
    return true;
  }
  if (!strcmp(key, "grid")) {
    val = constrain(val, 4, 48);
    if (val == grid) return false;
    grid = val;
    return true;
  }
  if (!strcmp(key, "thresh")) {
    val = constrain(val, 0, 255);
    if (val == thresh) return false;
    thresh = val;
    return true;
  }
  if (!strcmp(key, "hue")) {
    val = (val % 360 + 360) % 360;
    if (val == hue) return false;
    hue = val;
    return true;
  }
  if (!strcmp(key, "wob")) {
    val = constrain(val, -40, 40);
    if (val == wob) return false;
    wob = val;
    return true;
  }
  if (!strcmp(key, "ang")) {
    val = (val % 360 + 360) % 360;
    if (val > 180) val -= 360;
    if (val == ang) return false;
    ang = val;
    return true;
  }
  if (!strcmp(key, "mir")) {
    val = val ? 1 : 0;
    if (val == mir) return false;
    mir = val;
    return true;
  }
  if (!strcmp(key, "inv")) {
    val = val ? 1 : 0;
    if (val == inv) return false;
    inv = val;
    return true;
  }
  return false;
}

static void handleLine() {
  lineBuf[lineLen] = 0;
  if (!strcmp(lineBuf, "pm px")) {
    readingPx = true;
    pxGot = 0;
    pxDeadline = millis() + 250;
  } else if (!strncmp(lineBuf, "pm dump", 7)) {
    emitAll();
  } else if (!strncmp(lineBuf, "pm set ", 7)) {
    char key[16];
    int val = 0;
    if (sscanf(lineBuf + 7, "%15s %d", key, &val) == 2) assignKey(key, val);
  }
  lineLen = 0;
}

static void pollSerial() {
  while (Serial.available()) {
    if (readingPx) {
      while (readingPx && Serial.available() && pxGot < PX_N) {
        frame[pxGot++] = (uint8_t)Serial.read();
      }
      if (pxGot >= PX_N) {
        readingPx = false;
        frameValid = true;
        paintPending = true;
      } else if ((int32_t)(millis() - pxDeadline) > 0) {
        readingPx = false;
      }
      return;
    }
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (lineLen) handleLine();
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    }
  }
  if (readingPx && (int32_t)(millis() - pxDeadline) > 0) readingPx = false;
}

static void pollImu() {
  static uint32_t last = 0;
  static uint32_t nag = 0;
  if (millis() - last < 50) return;
  last = millis();

  if (!M5.Imu.isEnabled()) {
    if (millis() - nag > 1000) {
      nag = millis();
      Serial.printf("pm imu off %d\n", (int)M5.getBoard());
    }
    return;
  }

  float ax, ay, az, gx, gy, gz;
  bool ok = M5.Imu.getAccel(&ax, &ay, &az) && M5.Imu.getGyro(&gx, &gy, &gz);
  int16_t rax = M5.Imu.getRawData(0);
  int16_t ray = M5.Imu.getRawData(1);
  int16_t raz = M5.Imu.getRawData(2);
  int16_t rgx = M5.Imu.getRawData(3);
  int16_t rgy = M5.Imu.getRawData(4);
  int16_t rgz = M5.Imu.getRawData(5);
  Serial.printf("pm raw %d %d %d %d %d %d\n", rax, ray, raz, rgx, rgy, rgz);
  if (!ok) return;

  float roll = atan2f(ay, az) * 57.2958f;
  float pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * 57.2958f;
  Serial.printf("pm tilt %.1f %.1f %.1f %.1f %.1f\n", roll, pitch, gx, gy, gz);
}

static void resetAll() {
  mode = 0;
  grid = 14;
  thresh = 128;
  hue = 0;
  wob = 0;
  ang = 0;
  mir = 1;
  inv = 0;
  emitAll();
  showLeds();
}

void setup() {
  Serial.setRxBufferSize(2048);
  auto cfg = M5.config();
  cfg.fallback_board = m5::board_t::board_M5AtomMatrix;
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = true;
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  cfg.internal_rtc = false;
  cfg.external_display_value = 0;
  cfg.clear_display = true;
  M5.begin(cfg);
  M5.Led.setBrightness(64);
  M5.Imu.setCalibration(0, 0, 0);

  Serial.println();
  Serial.printf("pm hello atom %d imu %d\n", (int)M5.getBoard(), (int)M5.Imu.getType());
  emitAll();
  showLeds();
}

void loop() {
  M5.update();
  pollSerial();
  if (paintPending) {
    paintPending = false;
    showLeds();
  }
  pollImu();

  if (millis() - drewAt > 1000 && drewCount) {
    drewAt = millis();
    Serial.printf("pm drew %u\n", drewCount);
    drewCount = 0;
  }

  if (M5.BtnA.isPressed() && M5.BtnA.pressedFor(700)) {
    if (!holdA) {
      holdA = true;
      resetAll();
    }
  }
  if (M5.BtnA.wasReleased()) {
    if (!holdA) setMode(mode + 1);
    holdA = false;
  }

  delay(8);
}
