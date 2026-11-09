#include <M5Stack.h>

// Coarse 16x12 picture of the projector, plus the three buttons under the screen.
//
//   A  previous program
//   B  next program     (the center button)
//   C  chunkier / hold C finer
//   hold B  reset
//
// The browser sends "pm px" and then 192 bytes, one RGB332 pixel per cell.
// Tilt is reported as "pm imu ax ay az gx gy gz" and applied on the projector.

static const int COLS = 16;
static const int ROWS = 12;
static const int PX_N = COLS * ROWS;
static const int CELL = 20;

static const char *MODES[] = {"ascii dots", "color blocks", "edge glow", "mirror kaleido"};
static const int AMBER = 0xFD20;

static int mode = 0;
static int grid = 14;
static int thresh = 128;
static int hue = 0;
static int wob = 0;
static int ang = 0;
static int mir = 1;
static int inv = 0;

static bool imuOk = false;
static bool holdA = false;
static bool holdB = false;
static bool holdC = false;

static char lineBuf[80];
static uint8_t lineLen = 0;
static bool readingPx = false;
static int pxGot = 0;
static uint32_t pxDeadline = 0;
static uint8_t frame[PX_N];
static bool frameValid = false;
static bool paintPending = false;

static char banner[28];
static uint32_t bannerUntil = 0;
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

static void showBanner(const char *text) {
  strncpy(banner, text, sizeof(banner) - 1);
  banner[sizeof(banner) - 1] = 0;
  bannerUntil = millis() + 900;
}

static uint16_t rgb332(uint8_t c) {
  uint8_t r = (c >> 5) & 7;
  uint8_t g = (c >> 2) & 7;
  uint8_t b = c & 3;
  // Stop at about half scale. Full-scale 565 turns a bright camera into a white lamp.
  return ((uint16_t)(r * 16 / 7) << 11) | ((uint16_t)(g * 32 / 7) << 5) | (b * 16 / 3);
}

static void paintWaiting() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(GREEN, BLACK);
  M5.Lcd.setCursor(8, 16);
  M5.Lcd.print("pixel mirror");
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setCursor(8, 52);
  M5.Lcd.print(MODES[mode]);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(AMBER, BLACK);
  M5.Lcd.setCursor(8, 100);
  M5.Lcd.print("A  previous program");
  M5.Lcd.setCursor(8, 118);
  M5.Lcd.print("B  next program");
  M5.Lcd.setCursor(8, 136);
  M5.Lcd.print("C  chunkier    hold C finer");
  M5.Lcd.setTextColor(0x7BEF, BLACK);
  M5.Lcd.setCursor(8, 170);
  M5.Lcd.print("tilt and gyro show on the projector");
  M5.Lcd.setCursor(8, 210);
  M5.Lcd.print(frameValid ? "" : "waiting for the projector window");
}

static void paintFrame() {
  M5.Lcd.fillScreen(BLACK);
  for (int i = 0; i < PX_N; i++) {
    uint16_t color = rgb332(frame[i]);
    if (!color) continue;
    int col = i % COLS;
    int row = i / COLS;
    M5.Lcd.fillRect(col * CELL + 6, row * CELL + 6, 8, 8, color);
  }
  if (millis() < bannerUntil) {
    M5.Lcd.fillRect(0, 196, 320, 44, BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(AMBER, BLACK);
    M5.Lcd.setCursor(8, 208);
    M5.Lcd.print(banner);
  }
  drewCount++;
}

static void overlayBanner() {
  if (!frameValid || millis() >= bannerUntil) return;
  M5.Lcd.fillRect(0, 196, 320, 44, BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(AMBER, BLACK);
  M5.Lcd.setCursor(8, 208);
  M5.Lcd.print(banner);
}

static void setMode(int m) {
  mode = (m % 4 + 4) % 4;
  Serial.printf("pm set mode %d\n", mode);
  showBanner(MODES[mode]);
  if (frameValid) overlayBanner();
  else paintWaiting();
  M5.Speaker.beep();
}

static void setGrid(int g) {
  g = constrain(g, 4, 48);
  if (g == grid) {
    M5.Speaker.beep();
    return;
  }
  grid = g;
  Serial.printf("pm set grid %d\n", grid);
  char buf[20];
  snprintf(buf, sizeof(buf), "grid %d", grid);
  showBanner(buf);
  if (frameValid) overlayBanner();
  M5.Speaker.beep();
}

static bool assignKey(const char *key, int val) {
  if (!strcmp(key, "mode")) {
    val = constrain(val, 0, 3);
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
    val = constrain(val, 0, 40);
    if (val == wob) return false;
    wob = val;
    return true;
  }
  if (!strcmp(key, "ang")) {
    val = (val % 360 + 360) % 360;
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

static const char *imuErr = "off";
static float imuMag = 0;
static float gyroLsb = 131.f;

static void configureImu() {
  // Init already programs ±8 g and ±2000 deg/s. 2000/32768 is 16.4 counts per deg/s.
  gyroLsb = 16.4f;
}

static bool readTilt(float *roll, float *pitch, float *gx, float *gy, float *gz) {
  int16_t iax, iay, iaz, igx, igy, igz;
  M5.Mpu6886.getAccelAdc(&iax, &iay, &iaz);
  M5.Mpu6886.getGyroAdc(&igx, &igy, &igz);
  float ax = iax, ay = iay, az = iaz;
  float mag = sqrtf(ax * ax + ay * ay + az * az);
  imuMag = mag;
  imuErr = "mag";
  // 1 g is 16384 counts at ±2 g, 8192 at ±4 g, 4096 at ±8 g.
  float counts = 16384.f;
  if (mag < 12000.f && mag >= 6000.f) counts = 8192.f;
  else if (mag < 6000.f && mag >= 3000.f) counts = 4096.f;
  else if (mag < 3000.f) counts = 2048.f;
  if (fabsf(mag - counts) / counts > 0.4f) return false;
  // A slipped read shows up as a gyro axis stuck near a thousand deg/s.
  if (abs(igx) > 12000 || abs(igy) > 12000 || abs(igz) > 12000) {
    imuErr = "spin";
    return false;
  }
  imuErr = nullptr;

  ax /= counts;
  ay /= counts;
  az /= counts;
  *roll = atan2f(ay, az) * 57.2958f;
  *pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * 57.2958f;

  *gx = igx / gyroLsb;
  *gy = igy / gyroLsb;
  *gz = igz / gyroLsb;
  return true;
}

static void pollImu() {
  static uint32_t nag = 0;
  if (!imuOk) {
    if (millis() - nag > 1000) {
      nag = millis();
      Serial.printf("pm imu who %.0f\n", M5.Mpu6886.imuId);
      if ((int)M5.Mpu6886.imuId == 0x19) imuOk = true;
    }
    return;
  }
  static uint32_t last = 0;
  static bool primed = false;
  static float rollF = 0, pitchF = 0;
  static float bx = 0, by = 0, bz = 0;
  static int biasN = 0;
  if (millis() - last < 50) return;
  last = millis();

  float roll, pitch, gx, gy, gz;
  if (!readTilt(&roll, &pitch, &gx, &gy, &gz)) {
    if (millis() - nag > 1000) {
      nag = millis();
      Serial.printf("pm tilt %s %.0f\n", imuErr ? imuErr : "bad", imuMag);
    }
    return;
  }

  if (biasN < 24) {
    bx += gx;
    by += gy;
    bz += gz;
    biasN++;
    if (biasN == 24) {
      bx /= 24;
      by /= 24;
      bz /= 24;
    }
    return;
  }
  gx -= bx;
  gy -= by;
  gz -= bz;

  if (primed && (fabsf(roll - rollF) > 35.f || fabsf(pitch - pitchF) > 35.f)) return;

  if (!primed) {
    rollF = roll;
    pitchF = pitch;
    primed = true;
  } else {
    rollF += (roll - rollF) * 0.3f;
    pitchF += (pitch - pitchF) * 0.3f;
  }
  float rd = fabsf(rollF) < 1.5f ? 0 : rollF;
  float pd = fabsf(pitchF) < 1.5f ? 0 : pitchF;
  if (fabsf(gx) < 2) gx = 0;
  if (fabsf(gy) < 2) gy = 0;
  if (fabsf(gz) < 2) gz = 0;
  Serial.printf("pm tilt %.1f %.1f %.1f %.1f %.1f\n", rd, pd, gx, gy, gz);
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
  showBanner("reset");
  if (frameValid) paintFrame();
  else paintWaiting();
  M5.Speaker.beep();
}

void setup() {
  Serial.setRxBufferSize(2048);
  M5.begin(true, false, false, true);
  Serial.begin(115200);
  M5.Lcd.setRotation(1);
  M5.Lcd.setBrightness(120);
  pinMode(15, OUTPUT);
  digitalWrite(15, LOW);
  M5.Speaker.setBeep(1400, 30);
  imuOk = (M5.IMU.Init() == 0);
  if (imuOk) configureImu();
  Serial.println();
  Serial.printf("pm imu who %.0f\n", M5.Mpu6886.imuId);
  Serial.println("pm hello core");
  emitAll();
  paintWaiting();
}

void loop() {
  M5.update();
  pollSerial();
  if (paintPending) {
    paintPending = false;
    paintFrame();
  }
  pollImu();

  if (millis() - drewAt > 1000 && drewCount) {
    drewAt = millis();
    Serial.printf("pm drew %u\n", drewCount);
    drewCount = 0;
  }

  if (bannerUntil && millis() > bannerUntil) {
    bannerUntil = 0;
    if (frameValid) paintFrame();
  }

  if (M5.BtnA.wasReleased()) {
    if (!holdA) setMode(mode - 1);
    holdA = false;
  }
  if (M5.BtnA.isPressed() && M5.BtnA.pressedFor(700)) holdA = true;

  if (M5.BtnB.isPressed() && M5.BtnB.pressedFor(700)) {
    if (!holdB) {
      holdB = true;
      resetAll();
    }
  }
  if (M5.BtnB.wasReleased()) {
    if (!holdB) setMode(mode + 1);
    holdB = false;
  }

  if (M5.BtnC.isPressed() && M5.BtnC.pressedFor(500)) {
    if (!holdC) {
      holdC = true;
      setGrid(grid - 4);
    }
  }
  if (M5.BtnC.wasReleased()) {
    if (!holdC) setGrid(grid + 4);
    holdC = false;
  }

  delay(8);
}
