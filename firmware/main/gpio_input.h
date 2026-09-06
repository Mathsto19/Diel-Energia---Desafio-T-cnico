#ifndef GPIO_INPUT_H
#define GPIO_INPUT_H

#include "esp_err.h"

typedef void (*evento_botao_t)(void);

esp_err_t entrada_gpio_iniciar(evento_botao_t callback);

#endif
