/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "app_config.h"
#include "gpio_input.h"
#include "sensors.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
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
static uint32_t reconexoes_mqtt;
static uint32_t falhas_mqtt;
static bool mqtt_ja_conectou;

#define TAMANHO_COMANDO_CONFIG 512
#define TAMANHO_SOLICITACAO 64

typedef struct {
    char request_id[TAMANHO_SOLICITACAO];
    uint32_t intervalo_ms;
} comando_configuracao_t;

static QueueHandle_t fila_configuracao;
static uint32_t intervalo_leitura_ms = INTERVALO_LEITURA_MS;
static char buffer_configuracao[TAMANHO_COMANDO_CONFIG];
static size_t configuracao_recebida;
static size_t configuracao_total;

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

static bool adicionar_diagnosticos(cJSON *telemetria)
{
    cJSON *diagnosticos = cJSON_AddObjectToObject(telemetria, "diagnostics");
    wifi_ap_record_t informacoes_wifi;
    bool wifi_disponivel = esp_wifi_sta_get_ap_info(&informacoes_wifi) == ESP_OK;
    bool valido = diagnosticos != NULL &&
                  cJSON_AddNumberToObject(diagnosticos, "uptime_ms", obter_uptime_ms()) != NULL &&
                  cJSON_AddNumberToObject(diagnosticos, "free_heap_bytes", esp_get_free_heap_size()) != NULL &&
                  cJSON_AddNumberToObject(diagnosticos, "min_free_heap_bytes", esp_get_minimum_free_heap_size()) != NULL &&
                  cJSON_AddNumberToObject(diagnosticos, "outbox_bytes", cliente_mqtt != NULL ? esp_mqtt_client_get_outbox_size(cliente_mqtt) : 0) != NULL &&
                  cJSON_AddNumberToObject(diagnosticos, "mqtt_reconnections", reconexoes_mqtt) != NULL &&
                  cJSON_AddNumberToObject(diagnosticos, "mqtt_failures", falhas_mqtt) != NULL &&
                  cJSON_AddNumberToObject(diagnosticos, "enqueue_failures", falhas_enfileiramento) != NULL &&
                  cJSON_AddNumberToObject(diagnosticos, "outbox_full", outbox_cheio) != NULL &&
                  cJSON_AddNumberToObject(diagnosticos, "expired_messages", mensagens_expiradas) != NULL &&
                  cJSON_AddNumberToObject(diagnosticos, "gpio_events_dropped", entrada_gpio_eventos_perdidos()) != NULL &&
                  cJSON_AddNumberToObject(diagnosticos, "interval_ms", intervalo_leitura_ms) != NULL &&
                  cJSON_AddNumberToObject(diagnosticos, "telemetry_stack_free_words", uxTaskGetStackHighWaterMark(NULL)) != NULL;
    if (!valido) {
        return false;
    }
    if (wifi_disponivel) {
        return cJSON_AddNumberToObject(diagnosticos, "wifi_rssi_dbm", informacoes_wifi.rssi) != NULL;
    }
    return cJSON_AddStringToObject(diagnosticos, "wifi_rssi_dbm", "unavailable") != NULL;
}

static bool topico_configuracao(const esp_mqtt_event_handle_t evento)
{
    size_t tamanho_topico = strlen(TOPICO_CONFIGURACAO_SET);
    return evento->topic != NULL && evento->topic_len == (int)tamanho_topico &&
           memcmp(evento->topic, TOPICO_CONFIGURACAO_SET, tamanho_topico) == 0;
}

static void interpretar_comando_configuracao(void)
{
    cJSON *objeto = cJSON_ParseWithLength(buffer_configuracao, configuracao_total);
    if (objeto == NULL) {
        ESP_LOGW(TAG, "Comando de configuracao nao e um JSON valido");
        return;
    }
    cJSON *id = cJSON_GetObjectItemCaseSensitive(objeto, "request_id");
    cJSON *intervalo = cJSON_GetObjectItemCaseSensitive(objeto, "telemetry_interval_ms");
    bool intervalo_inteiro = intervalo != NULL && cJSON_IsNumber(intervalo) &&
                             intervalo->valuedouble >= 1000.0 && intervalo->valuedouble <= 60000.0 &&
                             intervalo->valuedouble == (double)intervalo->valueint;
    if (!cJSON_IsString(id) || id->valuestring == NULL || !intervalo_inteiro) {
        ESP_LOGW(TAG, "Configuracao rejeitada: request_id ou intervalo invalido");
        cJSON_Delete(objeto);
        return;
    }
    comando_configuracao_t comando = { 0 };
    strncpy(comando.request_id, id->valuestring, sizeof(comando.request_id) - 1);
    comando.intervalo_ms = (uint32_t)intervalo->valueint;
    if (xQueueSend(fila_configuracao, &comando, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Fila de configuracao cheia; comando rejeitado");
    }
    cJSON_Delete(objeto);
}

static void receber_fragmento_configuracao(const esp_mqtt_event_handle_t evento)
{
    if (evento->total_data_len <= 0 || evento->total_data_len > TAMANHO_COMANDO_CONFIG ||
        evento->current_data_offset < 0 || evento->data_len < 0 ||
        evento->current_data_offset + evento->data_len > evento->total_data_len ||
        evento->current_data_offset + evento->data_len > TAMANHO_COMANDO_CONFIG - 1) {
        ESP_LOGW(TAG, "Comando de configuracao excede o limite");
        configuracao_recebida = 0;
        configuracao_total = 0;
        return;
    }
    if (evento->current_data_offset == 0) {
        configuracao_recebida = 0;
        configuracao_total = (size_t)evento->total_data_len;
    }
    if (configuracao_total != (size_t)evento->total_data_len ||
        (size_t)evento->current_data_offset != configuracao_recebida) {
        ESP_LOGW(TAG, "Fragmento de configuracao fora de ordem");
        configuracao_recebida = 0;
        configuracao_total = 0;
        return;
    }
    memcpy(buffer_configuracao + configuracao_recebida, evento->data, (size_t)evento->data_len);
    configuracao_recebida += (size_t)evento->data_len;
    if (configuracao_recebida == configuracao_total) {
        buffer_configuracao[configuracao_total] = '\0';
        interpretar_comando_configuracao();
        configuracao_recebida = 0;
        configuracao_total = 0;
    }
}

static void enviar_confirmacao_configuracao(const comando_configuracao_t *comando)
{
    cJSON *ack = cJSON_CreateObject();
    bool valido = ack != NULL && adicionar_campo_comum(ack, "config_ack") &&
                  cJSON_AddStringToObject(ack, "request_id", comando->request_id) != NULL &&
                  cJSON_AddNumberToObject(ack, "telemetry_interval_ms", comando->intervalo_ms) != NULL &&
                  cJSON_AddBoolToObject(ack, "applied", true) != NULL;
    if (valido) {
        colocar_json_na_fila(TOPICO_CONFIGURACAO_ACK, ack);
    } else {
        ESP_LOGE(TAG, "Nao foi possivel criar a confirmacao de configuracao");
    }
    cJSON_Delete(ack);
}
static void tratar_evento_mqtt(void *argumentos, esp_event_base_t base, int32_t identificador_evento, void *dados_evento)
{
    (void) argumentos;
    (void) base;
    esp_mqtt_event_handle_t evento = dados_evento;
    esp_mqtt_client_handle_t cliente = evento->client;
    switch ((esp_mqtt_event_id_t)identificador_evento) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        if (mqtt_ja_conectou) {
            reconexoes_mqtt++;
        }
        mqtt_ja_conectou = true;
        esp_mqtt_client_subscribe(cliente, TOPICO_CONFIGURACAO_SET, 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        falhas_mqtt++;
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_DELETED:
        mensagens_expiradas++;
        ESP_LOGW(TAG, "Mensagem removida por expiracao, id=%d", evento->msg_id);
        break;
    case MQTT_EVENT_ERROR:
        falhas_mqtt++;
        ESP_LOGW(TAG, "MQTT_EVENT_ERROR");
        break;
    case MQTT_EVENT_DATA:
        if (topico_configuracao(evento)) {
            receber_fragmento_configuracao(evento);
        }
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
    if (valido) {
        colocar_json_na_fila(TOPICO_EVENTO, evento);
    } else {
        ESP_LOGE(TAG, "Nao foi possivel criar o evento do GPIO");
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
                          cJSON_AddStringToObject(telemetria, "sensor_mode", sensores_modo()) != NULL &&
                          cJSON_AddNumberToObject(telemetria, "temperature_c", leitura.temperatura_celsius) != NULL &&
                          cJSON_AddNumberToObject(telemetria, "humidity_pct", leitura.umidade_percentual) != NULL &&
                          cJSON_AddBoolToObject(telemetria, "sensors_valid", leitura.leitura_valida) != NULL &&
                          cJSON_AddNumberToObject(telemetria, "gpio_level", entrada_gpio_nivel_atual() ? 0 : 1) != NULL &&
                          adicionar_diagnosticos(telemetria);
            if (valido) {
                colocar_json_na_fila(TOPICO_TELEMETRIA, telemetria);
                ESP_LOGI(TAG, "Telemetria: %.1f C | %.1f %% | modo=%s | sensors_valid=%s | boot_id=%s | uptime_ms=%" PRIu32 " | intervalo=%" PRIu32 " ms",
                         leitura.temperatura_celsius, leitura.umidade_percentual, sensores_modo(),
                         leitura.leitura_valida ? "true" : "false", identificador_boot,
                         obter_uptime_ms(), intervalo_leitura_ms);
            } else {
                ESP_LOGE(TAG, "Nao foi possivel criar a telemetria JSON");
            }
            cJSON_Delete(telemetria);
        }

        comando_configuracao_t comando;
        if (xQueueReceive(fila_configuracao, &comando, pdMS_TO_TICKS(intervalo_leitura_ms)) == pdTRUE) {
            intervalo_leitura_ms = comando.intervalo_ms;
            enviar_confirmacao_configuracao(&comando);
            ESP_LOGI(TAG, "Intervalo atualizado para %" PRIu32 " ms", intervalo_leitura_ms);
        }
    }
}
static void sincronizar_relogio(void)
{
    esp_sntp_config_t configuracao = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    configuracao.start = false;
    esp_err_t erro = esp_netif_sntp_init(&configuracao);
    if (erro != ESP_OK && erro != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Nao foi possivel inicializar o SNTP: %s", esp_err_to_name(erro));
        return;
    }
    erro = esp_netif_sntp_start();
    if (erro != ESP_OK && erro != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Nao foi possivel iniciar o SNTP: %s", esp_err_to_name(erro));
        return;
    }
    ESP_LOGI(TAG, "Aguardando sincronizacao do relogio pelo SNTP");
    erro = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
    if (erro == ESP_OK) {
        time_t agora;
        time(&agora);
        ESP_LOGI(TAG, "Relogio sincronizado: %" PRIdMAX, (intmax_t)agora);
    } else {
        ESP_LOGW(TAG, "SNTP nao sincronizou no prazo; MQTT continuara tentando TLS");
    }
}
static void iniciar_mqtt(void)
{
    const esp_mqtt_client_config_t configuracao_mqtt = {
        .network = { .disable_auto_reconnect = false },
        .outbox = { .limit = 16 * 1024 },
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
    ESP_ERROR_CHECK(entrada_gpio_iniciar(tratar_evento_botao));
    fila_configuracao = xQueueCreate(4, sizeof(comando_configuracao_t));
    ESP_ERROR_CHECK(fila_configuracao != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(example_connect());
    sincronizar_relogio();
    iniciar_mqtt();
    ESP_ERROR_CHECK(sensores_iniciar());
    xTaskCreate(tarefa_telemetria, "tarefa_telemetria", 4096, NULL, 3, NULL);
}
