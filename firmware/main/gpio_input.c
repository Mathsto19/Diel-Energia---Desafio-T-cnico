#include "gpio_input.h"

#include "app_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static evento_botao_t callback_botao;

static void tarefa_botao(void *argumento)
{
    int estado_anterior = 1;
    (void) argumento;

    while (true) {
        int estado_atual = gpio_get_level(GPIO_BOTAO);
        if (estado_anterior == 1 && estado_atual == 0 && callback_botao != NULL) {
            callback_botao();
        }
        estado_anterior = estado_atual;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

esp_err_t entrada_gpio_iniciar(evento_botao_t callback)
{
    gpio_config_t configuracao = {
        .pin_bit_mask = 1ULL << GPIO_BOTAO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    callback_botao = callback;
    esp_err_t resultado = gpio_config(&configuracao);
    if (resultado != ESP_OK) {
        return resultado;
    }

    return xTaskCreate(tarefa_botao, "tarefa_botao", 2048, NULL, 5, NULL) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}
