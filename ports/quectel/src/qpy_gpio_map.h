#ifndef QPY_GPIO_MAP_H
#define QPY_GPIO_MAP_H

#include <stdint.h>

#include "qosa_gpio.h"

#define QPY_EXPORT_GPIO_COUNT (44)
#define QPY_GPIO_NO_SIMILAR_PIN (0xff)

typedef struct _qpy_gpio_map_t {
    uint8_t export_gpio;
    qosa_gpio_num_e gpio;
    uint8_t pin;
    uint8_t similar_pin;
} qpy_gpio_map_t;

const qpy_gpio_map_t *qpy_gpio_map_resolve(int export_gpio);
int qpy_gpio_map_select(const qpy_gpio_map_t *map);

#endif
