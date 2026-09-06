#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define IDENTIFICACAO_DISPOSITIVO "esp32-01-7c91"
#define GPIO_BOTAO 27
#define INTERVALO_LEITURA_MS 5000
#define MODBUS_TCP_HOST "host.wokwi.internal"
#define MODBUS_TCP_PORTA 5020
#define MODBUS_TCP_UNIDADE 1
#define MODBUS_TCP_TIMEOUT_MS 1000
#define TAMANHO_FILA_TELEMETRIA 4
#define TOPICO_TELEMETRIA "mathsto19/desafio/esp32-01-7c91/telemetry"
#define TOPICO_EVENTO "mathsto19/desafio/esp32-01-7c91/event"
#define TOPICO_CONFIGURACAO_SET "mathsto19/desafio/esp32-01-7c91/config/set"
#define TOPICO_CONFIGURACAO_ACK "mathsto19/desafio/esp32-01-7c91/config/ack"

#endif
