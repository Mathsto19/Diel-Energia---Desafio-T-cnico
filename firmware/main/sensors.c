#include "sensors.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "sensores";
static bool sensores_inicializados;

#if CONFIG_EXAMPLE_SENSORS_MODBUS_TCP
#include "esp_modbus_master.h"
#include "esp_timer.h"
#include "protocol_examples_common.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

typedef struct {
    leitura_sensores_t leitura;
    int64_t instante_us;
} amostra_modbus_t;

static QueueHandle_t fila_amostra;
/* Somente tarefa_modbus acessa o mestre; nunca destruir uma consulta em andamento. */
static void *mestre_modbus;
static bool modbus_inicializado;
static bool limpeza_falhou;
static char endereco_modbus[64];
static char *enderecos_modbus[] = { endereco_modbus, NULL };

typedef struct {
    uint16_t temperatura;
    uint16_t umidade;
} registradores_sensores_t;

#define HOLD_OFFSET(campo) ((uint32_t)(offsetof(registradores_sensores_t, campo) + 1))

/* ESP-Modbus 2.x exige os descritores antes de iniciar o mestre. */
static const mb_parameter_descriptor_t parametros_modbus[] = {
    {
        .cid = 0, .param_key = "temperatura", .param_units = "0.1 C",
        .mb_slave_addr = MODBUS_TCP_UNIDADE, .mb_param_type = MB_PARAM_HOLDING,
        .mb_reg_start = 0, .mb_size = 1,
        .param_offset = HOLD_OFFSET(temperatura), .param_type = PARAM_TYPE_U16,
        .param_size = sizeof(uint16_t), .param_opts = { .opt1 = 0, .opt2 = 1000, .opt3 = 1 },
        .access = PAR_PERMS_READ
    },
    {
        .cid = 1, .param_key = "umidade", .param_units = "0.1 %",
        .mb_slave_addr = MODBUS_TCP_UNIDADE, .mb_param_type = MB_PARAM_HOLDING,
        .mb_reg_start = 1, .mb_size = 1,
        .param_offset = HOLD_OFFSET(umidade), .param_type = PARAM_TYPE_U16,
        .param_size = sizeof(uint16_t), .param_opts = { .opt1 = 0, .opt2 = 1000, .opt3 = 1 },
        .access = PAR_PERMS_READ
    }
};

static esp_err_t preparar_endereco_modbus(void)
{
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *resultado = NULL;
    int retorno = getaddrinfo(MODBUS_TCP_HOST, NULL, &hints, &resultado);
    if (retorno != 0 || resultado == NULL) {
        ESP_LOGW(TAG, "Nao foi possivel resolver o host Modbus %s; nova tentativa em %d ms",
                 MODBUS_TCP_HOST, MODBUS_INTERVALO_REPETICAO_MS);
        return ESP_FAIL;
    }

    struct sockaddr_in *endereco = (struct sockaddr_in *)resultado->ai_addr;
    char ip[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &endereco->sin_addr, ip, sizeof(ip)) == NULL) {
        freeaddrinfo(resultado);
        return ESP_FAIL;
    }
    /* Resolver o host especial do Wokwi antes de montar UID;IPv4;PORTA. */
    int tamanho = snprintf(endereco_modbus, sizeof(endereco_modbus), "%d;%s;%d",
                           MODBUS_TCP_UNIDADE, ip, MODBUS_TCP_PORTA);
    freeaddrinfo(resultado);
    if (tamanho < 0 || tamanho >= (int)sizeof(endereco_modbus)) {
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "Host Modbus %s resolvido para %s", MODBUS_TCP_HOST, ip);
    return ESP_OK;
}

static esp_err_t iniciar_modbus(void)
{
    /* Delete com erro pode deixar objeto parcialmente liberado: nao reutilizar. */
    if (limpeza_falhou) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t erro = preparar_endereco_modbus();
    if (erro != ESP_OK) {
        return erro;
    }
    mb_communication_info_t configuracao = {
        .tcp_opts.port = MODBUS_TCP_PORTA,
        .tcp_opts.mode = MB_TCP,
        .tcp_opts.addr_type = MB_IPV4,
        .tcp_opts.ip_addr_table = enderecos_modbus,
        .tcp_opts.uid = 0,
        .tcp_opts.start_disconnected = true,
        .tcp_opts.response_tout_ms = MODBUS_TCP_TIMEOUT_MS,
        .tcp_opts.ip_netif_ptr = (void *)get_example_netif(),
    };
    erro = mbc_master_create_tcp(&configuracao, &mestre_modbus);
    if (erro != ESP_OK) {
        mestre_modbus = NULL;
        return erro;
    }
    erro = mbc_master_set_descriptor(mestre_modbus, parametros_modbus,
                                    sizeof(parametros_modbus) / sizeof(parametros_modbus[0]));
    if (erro == ESP_OK) {
        erro = mbc_master_start(mestre_modbus);
    }
    if (erro != ESP_OK) {
        esp_err_t limpeza = mbc_master_delete(mestre_modbus);
        limpeza_falhou = limpeza != ESP_OK;
        if (limpeza_falhou) {
            ESP_LOGE(TAG, "Falha ao liberar mestre Modbus: %s", esp_err_to_name(limpeza));
        }
        mestre_modbus = NULL;
        return erro;
    }
    modbus_inicializado = true;
    ESP_LOGI(TAG, "Mestre Modbus iniciado; conexao automatica em %s:%d, unidade %d",
             MODBUS_TCP_HOST, MODBUS_TCP_PORTA, MODBUS_TCP_UNIDADE);
    return ESP_OK;
}

static void tarefa_modbus(void *argumento)
{
    (void)argumento;
    bool ultima_valida = false;
    while (true) {
        amostra_modbus_t amostra = { 0 };
        esp_err_t erro = modbus_inicializado ? ESP_OK : iniciar_modbus();
        if (erro == ESP_OK) {
            mb_param_request_t requisicao = {
                .slave_addr = MODBUS_TCP_UNIDADE, .command = 0x03,
                .reg_start = 0, .reg_size = 2,
            };
            uint16_t registradores[2] = { 0 };
            erro = mbc_master_send_request(mestre_modbus, &requisicao, registradores);
            if (erro == ESP_OK) {
                amostra.leitura.temperatura_celsius = (float)registradores[0] / 10.0f;
                amostra.leitura.umidade_percentual = (float)registradores[1] / 10.0f;
                amostra.leitura.leitura_valida = true;
                amostra.instante_us = esp_timer_get_time();
                if (!ultima_valida) {
                    ESP_LOGI(TAG, "Modbus disponivel/recuperado: %.1f C, %.1f %%",
                             amostra.leitura.temperatura_celsius, amostra.leitura.umidade_percentual);
                }
            }
        }
        if (erro != ESP_OK) {
            ESP_LOGW(TAG, "Modbus indisponivel (%s); telemetria continua; nova tentativa em %d ms",
                     esp_err_to_name(erro), MODBUS_INTERVALO_REPETICAO_MS);
        }
        ultima_valida = amostra.leitura.leitura_valida;
        /* Fila de uma posicao: substituir inclusive por amostra invalida em falhas. */
        xQueueOverwrite(fila_amostra, &amostra);
        /* O driver TCP reconecta o mesmo mestre. Nao recriar a stack a cada timeout. */
        vTaskDelay(pdMS_TO_TICKS(erro == ESP_OK ? MODBUS_INTERVALO_CONSULTA_MS
                                              : MODBUS_INTERVALO_REPETICAO_MS));
    }
}
#else
static uint32_t contador_leituras;
#endif

esp_err_t sensores_iniciar(void)
{
    if (sensores_inicializados) {
        return ESP_OK;
    }
#if CONFIG_EXAMPLE_SENSORS_MODBUS_TCP
    fila_amostra = xQueueCreate(1, sizeof(amostra_modbus_t));
    if (fila_amostra == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(tarefa_modbus, "tarefa_modbus", 4096, NULL, 2, NULL) != pdPASS) {
        vQueueDelete(fila_amostra);
        fila_amostra = NULL;
        return ESP_ERR_NO_MEM;
    }
#else
    ESP_LOGI(TAG, "Sensores em modo simulado");
#endif
    sensores_inicializados = true;
    return ESP_OK;
}

esp_err_t sensores_ler(leitura_sensores_t *leitura)
{
    ESP_RETURN_ON_FALSE(leitura != NULL, ESP_ERR_INVALID_ARG, TAG, "Leitura invalida");
    ESP_RETURN_ON_FALSE(sensores_inicializados, ESP_ERR_INVALID_STATE, TAG, "Sensores nao inicializados");
#if CONFIG_EXAMPLE_SENSORS_MODBUS_TCP
    *leitura = (leitura_sensores_t){ 0 };
    amostra_modbus_t amostra;
    /* Nao esperar DNS, socket, semaforo do mestre ou leitura de registradores. */
    if (xQueuePeek(fila_amostra, &amostra, 0) == pdTRUE && amostra.leitura.leitura_valida &&
        esp_timer_get_time() - amostra.instante_us < (int64_t)MODBUS_VALIDADE_LEITURA_MS * 1000) {
        *leitura = amostra.leitura;
    }
#else
    uint32_t passo = contador_leituras++ % 20;
    leitura->temperatura_celsius = 25.0f + (float)passo * 0.2f;
    leitura->umidade_percentual = 60.0f + (float)passo * 0.5f;
    leitura->leitura_valida = true;
#endif
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
