#pragma once

// Название станка
#define MACHINE_NAME "My 3-Axis Lathe-Mill with C-axis"

// Количество осей: X, Z, C → 3 оси
#define N_AXIS 3

// === ОСЬ X: NEMA23 + DM556 ===
#define X_STEP_PIN          GPIO_NUM_12
#define X_DIRECTION_PIN     GPIO_NUM_14
#define X_DISABLE_PIN       GPIO_NUM_NC   // если есть, укажи; иначе NC

// === ОСЬ Z: NEMA23 + DM556 ===
#define Z_STEP_PIN          GPIO_NUM_27
#define Z_DIRECTION_PIN     GPIO_NUM_26
#define Z_DISABLE_PIN       GPIO_NUM_NC

// === ОСЬ C: HBS86H (closed-loop) ===
#define A_STEP_PIN          GPIO_NUM_32
#define A_DIRECTION_PIN     GPIO_NUM_33
#define A_DISABLE_PIN       GPIO_NUM_NC

// === Шпиндель: реле на 500 Вт ===
#define SPINDLE_TYPE        SpindleType::RELAY
#define SPINDLE_OUTPUT_PIN  GPIO_NUM_25

// === КОНЦЕВИКИ (раскомментируй, когда подключишь) ===
// #define X_LIMIT_PIN      GPIO_NUM_34
// #define Z_LIMIT_PIN      GPIO_NUM_35
// #define A_LIMIT_PIN      GPIO_NUM_36

// === Зонд (опционально) ===
// #define PROBE_PIN        GPIO_NUM_32

// === Отключаем неиспользуемые оси ===
#define Y_STEP_PIN          GPIO_NUM_NC
#define Y_DIRECTION_PIN     GPIO_NUM_NC
#define Y_DISABLE_PIN       GPIO_NUM_NC
#define B_STEP_PIN          GPIO_NUM_NC
#define B_DIRECTION_PIN     GPIO_NUM_NC