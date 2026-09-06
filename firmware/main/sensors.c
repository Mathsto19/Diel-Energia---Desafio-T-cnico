#include "sensors.h"

#include <stdint.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#if CONFIG_EXAMPLE_SENSORS_MODBUS_TCP
#include "esp_modbus_master.h"
#include "protocol_examples_common.h"
#endif

static const char *TAG = "sensores";
static SemaphoreHandle_t mutex_sensores;
static uint32_t contador_leituras;

#if CONFIG_EXAMPLE_SENSORS_MODBUS_TCP
static void *mestre_modbus;
static bool modbus_inicializado;
static char *enderecos_modbus[] = { MODBUS_TCP_HOST, NULL };
#endif

esp_err_t sensores_iniciar(void)
{
    mutex_sensores = xSemaphoreCreateMutex();
    if (mutex_sensores == NULL) {
        return ESP_ERR_NO_MEM;
    }

#if CONFIG_EXAMPLE_SENSORS_MODBUS_TCP
    mb_communication_info_t configuracao = {
        .tcp_opts.port = MODBUS_TCP_PORTA,
        .tcp_opts.mode = MB_TCP,
        .tcp_opts.addr_type = MB_IPV4,
        .tcp_opts.ip_addr_table = enderecos_modbus,
        .tcp_opts.uid = MODBUS_TCP_UNIDADE,
        .tcp_opts.start_disconnected = true,
        .tcp_opts.response_tout_ms = MODBUS_TCP_TIMEOUT_MS,
        .tcp_opts.ip_netif_ptr = (void *)get_example_netif(),
    };
    esp_err_t erro = mbc_master_create_tcp(&configuracao, &mestre_modbus);
    if (erro == ESP_OK) {
        erro = mbc_master_start(mestre_modbus);
    }
    if (erro != ESP_OK) {
        ESP_LOGE(TAG, "Nao foi possivel iniciar o mestre Modbus TCP: %s", esp_err_to_name(erro));
        mestre_modbus = NULL;
    } else {
        modbus_inicializado = true;
        ESP_LOGI(TAG, "Modbus TCP ativo em %s:%d, unidade %d", MODBUS_TCP_HOST, MODBUS_TCP_PORTA, MODBUS_TCP_UNIDADE);
    }
#else
    ESP_LOGI(TAG, "Sensores em modo simulado");
#endif
    return ESP_OK;
}

esp_err_t sensores_ler(leitura_sensores_t *leitura)
{
    ESP_RETURN_ON_FALSE(leitura != NULL, ESP_ERR_INVALID_ARG, TAG, "Leitura invalida");
    ESP_RETURN_ON_FALSE(mutex_sensores != NULL, ESP_ERR_INVALID_STATE, TAG, "Sensores nao inicializados");

    xSemaphoreTake(mutex_sensores, portMAX_DELAY);
    contador_leituras++;

#if CONFIG_EXAMPLE_SENSORS_MODBUS_TCP
    leitura->temperatura_celsius = 0.0f;
    leitura->umidade_percentual = 0.0f;
    leitura->leitura_valida = false;
    if (modbus_inicializado) {
        mb_param_request_t requisicao = {
            .slave_addr = MODBUS_TCP_UNIDADE,
            .command = 0x03,
            .reg_start = 0,
            .reg_size = 2,
        };
        uint16_t registradores[2] = { 0 };
        esp_err_t erro = mbc_master_send_request(mestre_modbus, &requisicao, registradores);
        if (erro == ESP_OK) {
            leitura->temperatura_celsius = (float)registradores[0] / 10.0f;
            leitura->umidade_percentual = (float)registradores[1] / 10.0f;
            leitura->leitura_valida = true;
        } else {
            ESP_LOGW(TAG, "Falha ao ler Modbus TCP: %s", esp_err_to_name(erro));
        }
    }
#else
    leitura->temperatura_celsius = 25.0f + (float)(contador_leituras - 1) * 0.2f;
    leitura->umidade_percentual = 60.0f + (float)(contador_leituras - 1) * 0.5f;
    leitura->leitura_valida = true;
#endif

    xSemaphoreGive(mutex_sensores);
    return ESP_OK;
}

const char *sensores_modo(void)
{
#if CONFIG_EXAMPLE_SENSORS_MODBUS_TCP
    return "modbus_tcp";
#else
    return "simulated";
#endif
}
