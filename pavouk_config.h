/*
 * pavouk_config.h – Board configuration and pin assignments for Pavouk robot
 *
 * To select a board, define one of the BOARD_* macros before including this file,
 * or set it in the Arduino IDE as a custom build flag.
 *
 * Currently supported boards:
 *   (default)                – EstarDyn ESP32-C3 MINI (original project board)
 *   BOARD_ESTARDYN_ESP32_C3  – explicit define for EstarDyn ESP32-C3 MINI; joystick
 *                              pin assignments below are PLACEHOLDERS and must be
 *                              updated once the physical hardware is available.
 *
 * ADC note (ESP32-C3):
 *   ESP32-C3 has only ADC1 (channels 0–4, GPIOs 0–4) and a limited set of
 *   ADC-capable pins. GPIO 0–4 can be used as analog inputs. Remap joystick
 *   pins accordingly when the board is available.
 */

#pragma once

// =============================================================================
// Default pin configuration (EstarDyn ESP32-C3 MINI)
// PR #19 added M5Unit Ultrasonic sensor; TRIG = GPIO 10, ECHO = GPIO 21 by default.
// This PR adds the config stub and hardware documentation.
// =============================================================================

// --- I2C (PCA9685 servo driver) ---
#ifndef I2C_SDA
  #define I2C_SDA  8
#endif
#ifndef I2C_SCL
  #define I2C_SCL  9
#endif

// --- Ultrasonic sensor (M5Unit Ultrasonic) ---
// Default pins from PR #19. GPIO 9 is reserved as BOOT button on ESP32-C3.
#ifndef ULTRASONIC_TRIG_PIN
  #define ULTRASONIC_TRIG_PIN  10
#endif
#ifndef ULTRASONIC_ECHO_PIN
  #define ULTRASONIC_ECHO_PIN  21
#endif

// =============================================================================
// Joystick / manual control pin placeholders
// Only activated when BOARD_ESTARDYN_ESP32_C3 is explicitly defined.
// These are PLACEHOLDER values – replace with actual GPIOs once the board and
// wiring are confirmed. ADC-capable pins on ESP32-C3 are GPIO 0–4.
// =============================================================================
#ifdef BOARD_ESTARDYN_ESP32_C3

// Joystick 1 (main directional joystick)
// PLACEHOLDER – replace with actual pin numbers before use!
#ifndef JOY1_X_PIN
  #define JOY1_X_PIN    0   // ADC1_CH0 – PLACEHOLDER
#endif
#ifndef JOY1_Y_PIN
  #define JOY1_Y_PIN    1   // ADC1_CH1 – PLACEHOLDER
#endif
#ifndef JOY1_BTN_PIN
  #define JOY1_BTN_PIN  6   // Digital – PLACEHOLDER
#endif

// Joystick 2 (secondary / optional)
// PLACEHOLDER – replace with actual pin numbers before use!
#ifndef JOY2_X_PIN
  #define JOY2_X_PIN    2   // ADC1_CH2 – PLACEHOLDER
#endif
#ifndef JOY2_Y_PIN
  #define JOY2_Y_PIN    3   // ADC1_CH3 – PLACEHOLDER
#endif
#ifndef JOY2_BTN_PIN
  #define JOY2_BTN_PIN  7   // Digital – PLACEHOLDER
#endif

// Servo signal pins (12 servos via PCA9685; direct GPIO pins listed as reference)
// PLACEHOLDER – servos are driven via PCA9685 over I2C (SDA/SCL above).
// If ever connecting servos directly to GPIO (not recommended for 12 servos),
// use: GPIO_S0..GPIO_S11 as placeholders below.
// static const int SERVO_PINS[12] = {
//     /* S0  */ -1,  /* S1  */ -1,  /* S2  */ -1,
//     /* S3  */ -1,  /* S4  */ -1,  /* S5  */ -1,
//     /* S6  */ -1,  /* S7  */ -1,  /* S8  */ -1,
//     /* S9  */ -1,  /* S10 */ -1,  /* S11 */ -1
// };  // PLACEHOLDER – replace -1 with actual GPIOs if bypassing PCA9685

#endif // BOARD_ESTARDYN_ESP32_C3
