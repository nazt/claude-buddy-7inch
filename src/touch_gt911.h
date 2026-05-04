#pragma once
#include <Arduino.h>
#include <Wire.h>

// GT911 touch — auto-detect addr 0x5D or 0x14
#define GT911_REG_STATUS 0x814E
#define GT911_REG_POINT0 0x8150

static uint8_t gt911_addr = 0;

static bool gt911ReadAt(uint8_t addr, uint16_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  size_t got = Wire.requestFrom((int)addr, (int)len);
  if (got != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static bool gt911WriteAt(uint8_t addr, uint16_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// Auto-probe addresses to find which one GT911 settled on
static void gt911Probe() {
  uint8_t prod[4];
  if (gt911ReadAt(0x5D, 0x8140, prod, 4)) { gt911_addr = 0x5D; return; }
  if (gt911ReadAt(0x14, 0x8140, prod, 4)) { gt911_addr = 0x14; return; }
  gt911_addr = 0;  // not found
}

static bool gt911GetTouch(int* x, int* y) {
  if (gt911_addr == 0) {
    gt911Probe();
    if (gt911_addr == 0) return false;
  }
  uint8_t status;
  if (!gt911ReadAt(gt911_addr, GT911_REG_STATUS, &status, 1)) return false;
  uint8_t nTouch = status & 0x0F;
  bool ready = (status & 0x80) != 0;
  if (!ready) return false;
  if (nTouch == 0) {
    gt911WriteAt(gt911_addr, GT911_REG_STATUS, 0);
    return false;
  }
  uint8_t pt[6];
  if (!gt911ReadAt(gt911_addr, GT911_REG_POINT0, pt, 6)) {
    gt911WriteAt(gt911_addr, GT911_REG_STATUS, 0);
    return false;
  }
  // This panel layout: POINT0 = [x_lo, x_hi, y_lo, y_hi, size_lo, size_hi]
  int rx = pt[0] | (pt[1] << 8);
  int ry = pt[2] | (pt[3] << 8);
  if (rx < 0 || rx >= 800 || ry < 0 || ry >= 480) {
    gt911WriteAt(gt911_addr, GT911_REG_STATUS, 0);
    return false;
  }
  *x = rx;
  *y = ry;
  gt911WriteAt(gt911_addr, GT911_REG_STATUS, 0);
  return true;
}
