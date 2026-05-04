#pragma once
#include <Wire.h>
#include <driver/gpio.h>

#define CH422G_MODE_ADDR   0x24
#define CH422G_OUTPUT_ADDR 0x38

// Bit map: TP_RST=1, LCD_BL=2, LCD_RST=3, SD_CS=4, USB_SEL=5

static void ch422gWriteOutput(uint8_t val) {
  Wire.beginTransmission(CH422G_OUTPUT_ADDR);
  Wire.write(val);
  Wire.endTransmission();
}

// Full board bring-up: CH422G + LCD reset + GT911 touch reset (with INT low to set addr=0x14)
static void ch422gInit(int sda = 8, int scl = 9) {
  Wire.begin(sda, scl, 400000);
  delay(10);

  // Set output mode
  Wire.beginTransmission(CH422G_MODE_ADDR);
  Wire.write(0x01);
  Wire.endTransmission();
  delay(10);

  // 1. Backlight + SD_CS HIGH, LCD_RST + TP_RST LOW (assert resets)
  // bits: SD_CS(4)=1, LCD_BL(2)=1 → 0x14
  ch422gWriteOutput(0x14);
  delay(10);

  // 2. GT911 address selection: hold INT(GPIO4) LOW during touch reset → I2C addr = 0x14
  gpio_reset_pin(GPIO_NUM_4);
  gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_NUM_4, 0);
  delay(2);

  // 3. Release TP_RST + LCD_RST while INT still low
  // bits 1,2,3,4 = TP_RST | LCD_BL | LCD_RST | SD_CS = 0x1E
  ch422gWriteOutput(0x1E);
  delay(10);

  // 4. Hold INT low a bit longer for GT911 to latch address
  delay(100);

  // 5. Release INT — set as input (high impedance)
  gpio_set_direction(GPIO_NUM_4, GPIO_MODE_INPUT);
  delay(50);
}
