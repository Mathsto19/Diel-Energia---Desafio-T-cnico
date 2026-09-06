/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "gpio_input.h"
#include "sensors.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "sdkconfig.h"

static const char *TAG = "telemetria";
static esp_mqtt_client_handle_t cliente_mqtt;
static char identificador_boot[9];
static uint32_t sequencia_mqtt;
static uint32_t falhas_enfileiramento;
static uint32_t outbox_cheio;
static uint32_t mensagens_expiradas;

#if CONFIG_EXAMPLE_BROKER_CERTIFICATE_OVERRIDDEN
static const char certificado_alternativo_pem[] =
    "-----BEGIN CERTIFICATE-----\n"
    CONFIG_EXAMPLE_BROKER_CERTIFICATE_OVERRIDE "\n"
    "-----END CERTIFICATE-----";
#endif

#if CONFIG_EXAMPLE_CERT_VALIDATE_MOSQUITTO_CA
extern const uint8_t mosquitto_org_crt_start[] asm("_binary_mosquitto_org_crt_start");
extern const uint8_t mosquitto_org_crt_end[] asm("_binary_mosquitto_org_crt_end");
#endif

static uint32_t obter_uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static esp_err_t colocar_json_na_fila(const char *topico, cJSON *objeto)
{
    char *texto = cJSON_PrintUnformatted(objeto);
    if (texto == NULL) {
        falhas_enfileiramento++;
        return ESP_ERR_NO_MEM;
    }

    int id = esp_mqtt_client_enqueue(cliente_mqtt, topico, texto, 0, 1, 0, true);
    cJSON_free(texto);
    if (id == -2) {
        outbox_cheio++;
        ESP_LOGW(TAG, "Outbox cheio; mensagem descartada");
        return ESP_ERR_NO_MEM;
    }
    if (id < 0) {
        falhas_enfileiramento++;
        ESP_LOGW(TAG, "Falha ao enfileirar mensagem MQTT: %d", id);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static bool adicionar_campo_comum(cJSON *objeto, const char *tipo)
{
    return cJSON_AddStringToObject(objeto, "type", tipo) != NULL &&
           cJSON_AddStringToObject(objeto, "device_id", IDENTIFICACAO_DISPOSITIVO) != NULL &&
           cJSON_AddStringToObject(objeto, "boot_id", identificador_boot) != NULL &&
           cJSON_AddNumberToObject(objeto, "seq", ++sequencia_mqtt) != NULL &&
           cJSON_AddNumberToObject(objeto, "uptime_ms", obter_uptime_ms()) != NULL;
}

static void tratar_evento_mqtt(void *argumentos, esp_event_base_t base, int32_t identificador_evento, void *dados_evento)
{
    (void) argumentos;
    (void) base;
    esp_mqtt_event_handle_t evento = dados_evento;
    esp_mqtt_client_handle_t cliente = evento->client;
    int identificador_mensagem;

    switch ((esp_mqtt_event_id_t)identificador_evento) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        identificador_mensagem = esp_mqtt_client_subscribe(cliente, TOPICO_CONFIGURACAO_SET, 1);
        ESP_LOGI(TAG, "Assinatura de configuracao enviada, id=%d", identificador_mensagem);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, id=%d", evento->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, id=%d", evento->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA: topico=%.*s dados=%.*s", evento->topic_len, evento->topic, evento->data_len, evento->data);
        break;
    case MQTT_EVENT_DELETED:
        mensagens_expiradas++;
        ESP_LOGW(TAG, "Mensagem removida por expiracao, id=%d", evento->msg_id);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        break;
    }
}

static void tratar_evento_botao(bool pressionado, uint32_t instante_ms)
{
    cJSON *evento = cJSON_CreateObject();
    bool valido = evento != NULL && adicionar_campo_comum(evento, "gpio_event") &&
                  cJSON_AddNumberToObject(evento, "pin", GPIO_BOTAO) != NULL &&
                  cJSON_AddNumberToObject(evento, "level", pressionado ? 0 : 1) != NULL;
    if (!valido) {
        cJSON_Delete(evento);
        ESP_LOGE(TAG, "Nao foi possivel criar o evento do GPIO");
        return;
    }
    if (colocar_json_na_fila(TOPICO_EVENTO, evento) != ESP_OK) {
        ESP_LOGW(TAG, "Evento GPIO descartado");
    }
    cJSON_Delete(evento);
    ESP_LOGI(TAG, "Evento GPIO: %s no pino %d em %" PRIu32 " ms", pressionado ? "pressionado" : "solto", GPIO_BOTAO, instante_ms);
}

static void tarefa_telemetria(void *argumento)
{
    leitura_sensores_t leitura;
    (void) argumento;

    while (true) {
        if (sensores_ler(&leitura) == ESP_OK && cliente_mqtt != NULL) {
            cJSON *telemetria = cJSON_CreateObject();
            bool valido = telemetria != NULL && adicionar_campo_comum(telemetria, "telemetry") &&
                          cJSON_AddStringToObject(telemetria, "sensor_mode", "simulated") != NULL &&
                          cJSON_AddNumberToObject(telemetria, "temperature_c", leitura.temperatura_celsius) != NULL &&
                          cJSON_AddNumberToObject(telemetria, "humidity_pct", leitura.umidade_percentual) != NULL &&
                          cJSON_AddBoolToObject(telemetria, "sensors_valid", leitura.leitura_valida) != NULL &&
                          cJSON_AddNumberToObject(telemetria, "gpio_level", entrada_gpio_nivel_atual() ? 0 : 1) != NULL;
            if (valido) {
                colocar_json_na_fila(TOPICO_TELEMETRIA, telemetria);
                ESP_LOGI(TAG, "Telemetria: %.1f C | %.1f %% | modo=simulated", leitura.temperatura_celsius, leitura.umidade_percentual);
            } else {
                ESP_LOGE(TAG, "Nao foi possivel criar a telemetria JSON");
            }
            cJSON_Delete(telemetria);
            ESP_LOGD(TAG, "Pendencias: falhas=%" PRIu32 ", outbox_cheio=%" PRIu32 ", expiradas=%" PRIu32 ", eventos_gpio_perdidos=%" PRIu32,
                     falhas_enfileiramento, outbox_cheio, mensagens_expiradas, entrada_gpio_eventos_perdidos());
        }
        vTaskDelay(pdMS_TO_TICKS(INTERVALO_LEITURA_MS));
    }
}

static void iniciar_mqtt(void)
{
    const esp_mqtt_client_config_t configuracao_mqtt = {
        .network = {
            .disable_auto_reconnect = false,
        },
        .outbox = {
            .limit = 16 * 1024,
        },
        .broker = {
            .address.uri = CONFIG_EXAMPLE_MQTT_BROKER_URI,
#if CONFIG_EXAMPLE_BROKER_CERTIFICATE_OVERRIDDEN
            .verification.certificate = certificado_alternativo_pem,
#elif CONFIG_EXAMPLE_CERT_VALIDATE_MOSQUITTO_CA
            .verification.certificate = (const char *)mosquitto_org_crt_start,
#else
            .verification.crt_bundle_attach = esp_crt_bundle_attach,
#endif
        },
    };

    cliente_mqtt = esp_mqtt_client_init(&configuracao_mqtt);
    ESP_ERROR_CHECK(cliente_mqtt != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(cliente_mqtt, ESP_EVENT_ANY_ID, tratar_evento_mqtt, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(cliente_mqtt));
}

void app_main(void)
{
    snprintf(identificador_boot, sizeof(identificador_boot), "%04" PRIx32, (uint32_t)(esp_random() & 0xffffU));
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(sensores_iniciar());
    ESP_ERROR_CHECK(entrada_gpio_iniciar(tratar_evento_botao));
    iniciar_mqtt();
    xTaskCreate(tarefa_telemetria, "tarefa_telemetria", 4096, NULL, 3, NULL);
    ESP_ERROR_CHECK(example_connect());
}
