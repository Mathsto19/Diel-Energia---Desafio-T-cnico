#include "gpio_input.h"

#include "app_config.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TEMPO_DEBOUNCE_MS 30
#define PERIODO_LEITURA_MS 10

static const char *TAG = "entrada_gpio";

static evento_botao_t callback_evento;
static volatile bool botao_pressionado;
static volatile uint32_t eventos_perdidos;

static void tarefa_botao(void *argumento)
{
    (void)argumento;

    int ultimo_nivel_lido = gpio_get_level(GPIO_BOTAO);
    int nivel_estavel = ultimo_nivel_lido;

    TickType_t instante_ultima_mudanca = xTaskGetTickCount();

    ESP_LOGI(
        TAG,
        "Monitorando GPIO %d | nivel inicial=%d",
        GPIO_BOTAO,
        ultimo_nivel_lido
    );

    while (true) {

        int nivel_lido = gpio_get_level(GPIO_BOTAO);

        /*
         * Detectou mudança elétrica no pino.
         */
        if (nivel_lido != ultimo_nivel_lido) {

            ESP_LOGI(
                TAG,
                "GPIO %d mudou fisicamente: %d -> %d",
                GPIO_BOTAO,
                ultimo_nivel_lido,
                nivel_lido
            );

            ultimo_nivel_lido = nivel_lido;
            instante_ultima_mudanca = xTaskGetTickCount();
        }

        /*
         * Só considera a mudança válida depois do debounce.
         */
        if (nivel_lido != nivel_estavel) {

            TickType_t tempo_decorrido =
                xTaskGetTickCount() - instante_ultima_mudanca;

            if (tempo_decorrido >= pdMS_TO_TICKS(TEMPO_DEBOUNCE_MS)) {

                nivel_estavel = nivel_lido;

                bool pressionado = (nivel_estavel == 0);

                __atomic_store_n(
                    &botao_pressionado,
                    pressionado,
                    __ATOMIC_RELAXED
                );

                uint32_t instante_ms =
                    (uint32_t)(esp_timer_get_time() / 1000ULL);

                ESP_LOGI(
                    TAG,
                    "BOTAO %s | GPIO=%d | nivel=%d | tempo=%lu ms",
                    pressionado ? "PRESSIONADO" : "SOLTO",
                    GPIO_BOTAO,
                    nivel_estavel,
                    (unsigned long)instante_ms
                );

                if (callback_evento != NULL) {
                    callback_evento(
                        pressionado,
                        instante_ms
                    );
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(PERIODO_LEITURA_MS));
    }
}

esp_err_t entrada_gpio_iniciar(evento_botao_t callback)
{
    callback_evento = callback;

    gpio_config_t configuracao = {
        .pin_bit_mask = (1ULL << GPIO_BOTAO),
        .mode = GPIO_MODE_INPUT,

        /*
         * Botão ligado entre GPIO 27 e GND.
         * Solto = 1
         * Pressionado = 0
         */
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,

        /*
         * Por enquanto não usamos interrupção.
         * A task faz leitura direta para simplificar
         * e tornar o diagnóstico determinístico.
         */
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t erro = gpio_config(&configuracao);

    if (erro != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Falha ao configurar GPIO %d: %s",
            GPIO_BOTAO,
            esp_err_to_name(erro)
        );

        return erro;
    }

    int nivel_inicial = gpio_get_level(GPIO_BOTAO);

    __atomic_store_n(
        &botao_pressionado,
        nivel_inicial == 0,
        __ATOMIC_RELAXED
    );

    ESP_LOGI(
        TAG,
        "GPIO %d configurado como entrada com pull-up | nivel=%d",
        GPIO_BOTAO,
        nivel_inicial
    );

    BaseType_t resultado = xTaskCreate(
        tarefa_botao,
        "tarefa_botao",
        3072,
        NULL,
        5,
        NULL
    );

    if (resultado != pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar tarefa do botao");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

bool entrada_gpio_nivel_atual(void)
{
    return __atomic_load_n(
        &botao_pressionado,
        __ATOMIC_RELAXED
    );
}

uint32_t entrada_gpio_eventos_perdidos(void)
{
    return __atomic_load_n(
        &eventos_perdidos,
        __ATOMIC_RELAXED
    );
}