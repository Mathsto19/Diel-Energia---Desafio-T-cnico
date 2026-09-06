#ifndef SENSORS_H
#define SENSORS_H

#include "esp_err.h"

typedef struct {
    float temperatura_celsius;
    float umidade_percentual;
} leitura_sensores_t;

esp_err_t sensores_iniciar(void);
esp_err_t sensores_ler(leitura_sensores_t *leitura);

#endif
