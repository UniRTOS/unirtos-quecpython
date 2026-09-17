#include <stddef.h>

#include "qosa_gpio.h"
#include "qosa_pinctrl.h"

#include "qpy_gpio_map.h"

#define QPY_GPIO_MAP_ENTRY(exported, internal, physical, similar) \
    { exported, QOSA_GPIO_##internal, physical, similar }

/*
 * Keep the exported Helios GPIO number as the public identifier.  The physical
 * pin is retained because multiple pins can share one QOSA GPIO number.
 */
#if defined(QPY_BOARD_EG800ZCN_LA) || defined(QPY_BOARD_EG800ZGL_LA)
static const qpy_gpio_map_t qpy_gpio_map[] = {
    QPY_GPIO_MAP_ENTRY(1, 29, 30, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(2, 30, 31, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(3, 31, 32, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(4, 32, 33, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(5, 15, 49, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(6, 37, 50, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(7, 38, 51, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(8, 35, 52, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(9, 34, 53, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(10, 3, 54, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(11, 6, 55, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(12, 7, 56, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(15, 4, 80, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(16, 5, 81, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(17, 26, 25, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(23, 8, 66, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(24, 9, 67, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(25, 18, 17, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(26, 19, 18, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(27, 22, 19, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(28, 27, 20, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(30, 1, 22, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(31, 2, 23, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(32, 10, 28, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(33, 11, 29, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(34, 16, 38, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(35, 17, 39, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(36, 25, 16, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(37, 36, 78, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(38, 21, 6, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(39, 20, 5, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(40, 23, 100, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(41, 24, 101, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(42, 33, 26, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(43, 12, 64, QPY_GPIO_NO_SIMILAR_PIN),
};
#elif defined(QPY_BOARD_EC800ZCN_LF)
static const qpy_gpio_map_t qpy_gpio_map[] = {
    QPY_GPIO_MAP_ENTRY(1, 29, 30, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(2, 30, 31, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(3, 31, 32, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(4, 32, 33, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(6, 13, 50, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(7, 14, 51, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(8, 12, 52, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(9, 15, 53, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(10, 3, 54, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(12, 7, 56, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(13, 35, 57, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(14, 34, 58, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(17, 26, 25, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(23, 19, 66, 18),
    QPY_GPIO_MAP_ENTRY(24, 18, 67, 17),
    QPY_GPIO_MAP_ENTRY(25, 18, 17, 67),
    QPY_GPIO_MAP_ENTRY(26, 19, 18, 66),
    QPY_GPIO_MAP_ENTRY(27, 22, 19, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(28, 28, 20, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(29, 23, 21, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(30, 1, 22, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(31, 2, 23, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(32, 10, 28, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(33, 11, 29, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(34, 16, 38, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(35, 17, 39, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(36, 27, 16, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(37, 36, 78, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(38, 25, 85, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(39, 21, 108, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(40, 20, 109, QPY_GPIO_NO_SIMILAR_PIN),
    QPY_GPIO_MAP_ENTRY(41, 9, 101, QPY_GPIO_NO_SIMILAR_PIN),
};
#else
#error "No exported GPIO map is defined for this QuecPython board"
#endif

const qpy_gpio_map_t *qpy_gpio_map_resolve(int export_gpio) {
    for (size_t i = 0; i < sizeof(qpy_gpio_map) / sizeof(qpy_gpio_map[0]); ++i) {
        if (qpy_gpio_map[i].export_gpio == export_gpio) {
            return &qpy_gpio_map[i];
        }
    }
    return NULL;
}

static int qpy_gpio_map_restore_default(uint8_t pin) {
    qosa_pin_cfg_t cfg;
    if (qosa_get_pin_default_cfg(pin, &cfg) != QOSA_GPIO_SUCCESS) {
        return -1;
    }
    return qosa_pin_set_func((qosa_pin_num_e)pin, cfg.default_func) == QOSA_PINCTRL_SUCCESS ? 0 : -1;
}

int qpy_gpio_map_select(const qpy_gpio_map_t *map) {
    qosa_pin_cfg_t cfg;
    if (map == NULL || qosa_get_pin_default_cfg(map->pin, &cfg) != QOSA_GPIO_SUCCESS || cfg.gpio_num != map->gpio) {
        return -1;
    }
    if (map->similar_pin != QPY_GPIO_NO_SIMILAR_PIN && qpy_gpio_map_restore_default(map->similar_pin) != 0) {
        return -1;
    }
    return qosa_pin_set_func((qosa_pin_num_e)map->pin, cfg.gpio_func) == QOSA_PINCTRL_SUCCESS ? 0 : -1;
}
