#include "sensors.h"

#include <stddef.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t mutex_sensores;
static uint32_t contador_leituras;

esp_err_t sensores_iniciar(void)
{
    mutex_sensores = xSemaphoreCreateMutex();
    return mutex_sensores != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t sensores_ler(leitura_sensores_t *leitura)
{
    ESP_RETURN_ON_FALSE(leitura != NULL, ESP_ERR_INVALID_ARG, "sensores", "Leitura invalida");
    ESP_RETURN_ON_FALSE(mutex_sensores != NULL, ESP_ERR_INVALID_STATE, "sensores", "Sensores nao inicializados");

    xSemaphoreTake(mutex_sensores, portMAX_DELAY);
    contador_leituras++;
    leitura->temperatura_celsius = 24.0f + (float)(contador_leituras % 10) * 0.3f;
    leitura->umidade_percentual = 55.0f + (float)(contador_leituras % 8) * 0.7f;
    xSemaphoreGive(mutex_sensores);
    return ESP_OK;
}
