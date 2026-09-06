/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Exemplo de MQTT com TLS.
   Aviso de licenca original preservado abaixo.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "gpio_input.h"
#include "sensors.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "protocol_examples_common.h"

#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "mqtt_client.h"

static const char *TAG = "telemetria";
static esp_mqtt_client_handle_t cliente_mqtt;
static char identificador_boot[9];
static uint32_t sequencia_mqtt;

static uint32_t obter_uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static esp_err_t colocar_json_na_fila(const char *topico, cJSON *objeto)
{
    char *texto = cJSON_PrintUnformatted(objeto);
    if (texto == NULL) {
        return ESP_ERR_NO_MEM;
    }
    int id = esp_mqtt_client_enqueue(cliente_mqtt, topico, texto, 0, 1, 0, true);
    cJSON_free(texto);
    return id >= 0 ? ESP_OK : ESP_FAIL;
}

static bool adicionar_campo_comum(cJSON *objeto, const char *tipo)
{
    return cJSON_AddStringToObject(objeto, "type", tipo) != NULL &&
           cJSON_AddStringToObject(objeto, "device_id", IDENTIFICACAO_DISPOSITIVO) != NULL &&
           cJSON_AddStringToObject(objeto, "boot_id", identificador_boot) != NULL &&
           cJSON_AddNumberToObject(objeto, "seq", ++sequencia_mqtt) != NULL &&
           cJSON_AddNumberToObject(objeto, "uptime_ms", obter_uptime_ms()) != NULL;
}

#if CONFIG_EXAMPLE_BROKER_CERTIFICATE_OVERRIDDEN
static const char certificado_alternativo_pem[] =
    "-----BEGIN CERTIFICATE-----\n"
    CONFIG_EXAMPLE_BROKER_CERTIFICATE_OVERRIDE "\n"
    "-----END CERTIFICATE-----";
#endif

#if CONFIG_EXAMPLE_CERT_VALIDATE_MOSQUITTO_CA
/* Certificado da autoridade Mosquitto incorporado para test.mosquitto.org:8883 */
extern const uint8_t mosquitto_org_crt_start[] asm("_binary_mosquitto_org_crt_start");
extern const uint8_t mosquitto_org_crt_end[] asm("_binary_mosquitto_org_crt_end");
#endif

/*
 * @brief Funcao registrada para tratar eventos MQTT
 *
 *  O loop de eventos do cliente MQTT chama esta funcao.
 *
 * @param argumentos dados do usuario associados ao evento.
 * @param base base de eventos (MQTT neste exemplo).
 * @param identificador_evento identificador do evento recebido.
 * @param dados_evento dados do evento, do tipo esp_mqtt_event_handle_t.
 */
static void tratar_evento_mqtt(void *argumentos, esp_event_base_t base, int32_t identificador_evento, void *dados_evento)
{
    ESP_LOGD(TAG, "Evento recebido: base=%s, identificador=%" PRIi32, base, identificador_evento);
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
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, identificador_mensagem=%d, codigo de retorno=0x%02x ", evento->msg_id, (uint8_t)*evento->data);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, identificador_mensagem=%d", evento->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, identificador_mensagem=%d", evento->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPICO=%.*s\r\n", evento->topic_len, evento->topic);
        printf("DADOS=%.*s\r\n", evento->data_len, evento->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (evento->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGI(TAG, "Ultimo codigo de erro do esp-tls: 0x%x", evento->error_handle->esp_tls_last_esp_err);
            ESP_LOGI(TAG, "Ultimo erro da pilha TLS: 0x%x", evento->error_handle->esp_tls_stack_err);
            ESP_LOGI(TAG, "Ultimo errno capturado: %d (%s)", evento->error_handle->esp_transport_sock_errno,
                     strerror(evento->error_handle->esp_transport_sock_errno));
        } else if (evento->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGI(TAG, "Conexao recusada: 0x%x", evento->error_handle->connect_return_code);
        } else {
            ESP_LOGW(TAG, "Tipo de erro desconhecido: 0x%x", evento->error_handle->error_type);
        }
        break;
    default:
        ESP_LOGI(TAG, "Outro evento, id:%d", evento->event_id);
        break;
    }
}

static void tratar_evento_botao(bool pressionado, uint32_t instante_ms)
{
    cJSON *evento = cJSON_CreateObject();
    if (evento == NULL || !adicionar_campo_comum(evento, "gpio_event") ||
        cJSON_AddNumberToObject(evento, "pin", GPIO_BOTAO) == NULL ||
        cJSON_AddNumberToObject(evento, "level", pressionado ? 0 : 1) == NULL) {
        cJSON_Delete(evento);
        ESP_LOGE(TAG, "Nao foi possivel criar o evento do GPIO");
        return;
    }
    if (colocar_json_na_fila(TOPICO_EVENTO, evento) != ESP_OK) {
        ESP_LOGW(TAG, "Fila MQTT cheia ao registrar evento do GPIO");
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
            bool campos_validos = telemetria != NULL && adicionar_campo_comum(telemetria, "telemetry") &&
                                  cJSON_AddStringToObject(telemetria, "sensor_mode", "simulated") != NULL &&
                                  cJSON_AddNumberToObject(telemetria, "temperature_c", leitura.temperatura_celsius) != NULL &&
                                  cJSON_AddNumberToObject(telemetria, "humidity_pct", leitura.umidade_percentual) != NULL &&
                                  cJSON_AddBoolToObject(telemetria, "sensors_valid", leitura.leitura_valida) != NULL &&
                                  cJSON_AddNumberToObject(telemetria, "gpio_level", entrada_gpio_nivel_atual() ? 0 : 1) != NULL;
            if (campos_validos) {
                if (colocar_json_na_fila(TOPICO_TELEMETRIA, telemetria) != ESP_OK) {
                    ESP_LOGW(TAG, "Fila MQTT cheia para a telemetria");
                } else {
                    ESP_LOGI(TAG, "Telemetria simulada: %.1f C | %.1f %%", leitura.temperatura_celsius, leitura.umidade_percentual);
                }
            } else {
                ESP_LOGE(TAG, "Nao foi possivel criar a telemetria JSON");
            }
            cJSON_Delete(telemetria);
        }
        vTaskDelay(pdMS_TO_TICKS(INTERVALO_LEITURA_MS));
    }
}
static void iniciar_mqtt(void)
{
    const esp_mqtt_client_config_t configuracao_mqtt = {
        .broker = {
            .address.uri = CONFIG_EXAMPLE_MQTT_BROKER_URI,
#if CONFIG_EXAMPLE_BROKER_CERTIFICATE_OVERRIDDEN
            .verification.certificate = certificado_alternativo_pem,
#elif CONFIG_EXAMPLE_CERT_VALIDATE_MOSQUITTO_CA
            .verification.certificate = (const char *)mosquitto_org_crt_start,
#else
            .verification.crt_bundle_attach = esp_crt_bundle_attach, /* Usa o conjunto de certificados do ESP-IDF */
#endif
        },
    };

    ESP_LOGI(TAG, "[APP] Memoria livre: %" PRIu32 " bytes", esp_get_free_heap_size());
    cliente_mqtt = esp_mqtt_client_init(&configuracao_mqtt);
    /* O ultimo argumento permite passar dados para tratar_evento_mqtt */
    esp_mqtt_client_register_event(cliente_mqtt, ESP_EVENT_ANY_ID, tratar_evento_mqtt, NULL);
    esp_mqtt_client_start(cliente_mqtt);
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Iniciando...");
    ESP_LOGI(TAG, "[APP] Memoria livre: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] Versao do ESP-IDF: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Configura Wi-Fi ou Ethernet conforme selecionado no menuconfig.
     * Consulte a secao sobre conexao Wi-Fi ou Ethernet em
     * examples/protocols/README.md para detalhes desta funcao.
     */
    ESP_ERROR_CHECK(example_connect());

    snprintf(identificador_boot, sizeof(identificador_boot), "%04" PRIx32, (uint32_t)(esp_random() & 0xffffU));
    ESP_ERROR_CHECK(sensores_iniciar());
    ESP_ERROR_CHECK(entrada_gpio_iniciar(tratar_evento_botao));
    iniciar_mqtt();
    xTaskCreate(tarefa_telemetria, "tarefa_telemetria", 4096, NULL, 3, NULL);
}
