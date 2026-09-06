#include "gpio_input.h"

#include "app_config.h"
#include "button_gpio.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "iot_button.h"

#define TAMANHO_FILA_EVENTOS_BOTAO 8

typedef struct {
    bool pressionado;
    uint32_t instante_ms;
} evento_botao_fila_t;

static QueueHandle_t fila_eventos;
static SemaphoreHandle_t mutex_estado;
static button_handle_t dispositivo_botao;
static evento_botao_t callback_evento;
static bool nivel_atual;
static volatile uint32_t eventos_perdidos;

static void callback_botao(void *dispositivo, void *dados)
{
    evento_botao_fila_t evento = {
        .pressionado = iot_button_get_event(dispositivo) == BUTTON_PRESS_DOWN,
        .instante_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
    };
    (void) dados;
    if (xQueueSend(fila_eventos, &evento, 0) != pdTRUE) {
        __atomic_fetch_add(&eventos_perdidos, 1, __ATOMIC_RELAXED);
    }
}

static void tarefa_eventos_gpio(void *argumento)
{
    evento_botao_fila_t evento;
    (void) argumento;

    while (xQueueReceive(fila_eventos, &evento, portMAX_DELAY) == pdTRUE) {
        xSemaphoreTake(mutex_estado, portMAX_DELAY);
        nivel_atual = evento.pressionado;
        xSemaphoreGive(mutex_estado);
        if (callback_evento != NULL) {
            callback_evento(evento.pressionado, evento.instante_ms);
        }
    }
}

esp_err_t entrada_gpio_iniciar(evento_botao_t callback)
{
    const button_config_t configuracao_botao = {0};
    const button_gpio_config_t configuracao_gpio = {
        .gpio_num = GPIO_BOTAO,
        .active_level = 0,
    };

    fila_eventos = xQueueCreate(TAMANHO_FILA_EVENTOS_BOTAO, sizeof(evento_botao_fila_t));
    mutex_estado = xSemaphoreCreateMutex();
    if (fila_eventos == NULL || mutex_estado == NULL) {
        return ESP_ERR_NO_MEM;
    }

    callback_evento = callback;
    ESP_RETURN_ON_ERROR(iot_button_new_gpio_device(&configuracao_botao, &configuracao_gpio, &dispositivo_botao),
                        "entrada_gpio", "Falha ao criar botao GPIO");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(dispositivo_botao, BUTTON_PRESS_DOWN, NULL, callback_botao, NULL),
                        "entrada_gpio", "Falha ao registrar evento de pressionar");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(dispositivo_botao, BUTTON_PRESS_UP, NULL, callback_botao, NULL),
                        "entrada_gpio", "Falha ao registrar evento de soltar");

    xSemaphoreTake(mutex_estado, portMAX_DELAY);
    nivel_atual = iot_button_get_key_level(dispositivo_botao) == 0;
    xSemaphoreGive(mutex_estado);

    return xTaskCreate(tarefa_eventos_gpio, "eventos_gpio", 3072, NULL, 5, NULL) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

bool entrada_gpio_nivel_atual(void)
{
    bool nivel;
    xSemaphoreTake(mutex_estado, portMAX_DELAY);
    nivel = nivel_atual;
    xSemaphoreGive(mutex_estado);
    return nivel;
}

uint32_t entrada_gpio_eventos_perdidos(void)
{
    return __atomic_load_n(&eventos_perdidos, __ATOMIC_RELAXED);
}
