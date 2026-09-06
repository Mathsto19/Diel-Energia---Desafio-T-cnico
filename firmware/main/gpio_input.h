#ifndef GPIO_INPUT_H
#define GPIO_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef void (*evento_botao_t)(bool pressionado, uint32_t instante_ms);

esp_err_t entrada_gpio_iniciar(evento_botao_t callback);
bool entrada_gpio_nivel_atual(void);

#endif
