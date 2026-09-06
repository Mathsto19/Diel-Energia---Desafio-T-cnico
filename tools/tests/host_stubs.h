/* Dubles somente para testar sensors.c no computador, sem ESP32 ou rede. */
#ifndef HOST_STUBS_H
#define HOST_STUBS_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_STATE 3
#define ESP_ERR_INVALID_SIZE 4
#define ESP_ERR_TIMEOUT 5
#define ESP_RETURN_ON_FALSE(check, error, ...) do { if (!(check)) return error; } while (0)
#define ESP_LOGI(tag, ...) do { (void)(tag); printf(__VA_ARGS__); puts(""); } while (0)
#define ESP_LOGW ESP_LOGI
#define ESP_LOGE ESP_LOGI
static inline const char *esp_err_to_name(int e) { return e == ESP_OK ? "ESP_OK" : "erro injetado"; }
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned count, unsigned size);
int xQueuePeek(QueueHandle_t queue, void *value, unsigned wait);
int xQueueOverwrite(QueueHandle_t queue, const void *value);
void vQueueDelete(QueueHandle_t queue);
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned prio, void *handle);
void vTaskDelay(unsigned ticks);
int64_t esp_timer_get_time(void);
#define AF_INET 2
#define SOCK_STREAM 1
#define INET_ADDRSTRLEN 16
struct in_addr { unsigned value; };
struct sockaddr_in { struct in_addr sin_addr; };
struct addrinfo { int ai_family, ai_socktype; void *ai_addr; };
int getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);
void freeaddrinfo(struct addrinfo *);
const char *inet_ntop(int, const void *, char *, size_t);
void *get_example_netif(void);
#define MB_PARAM_HOLDING 0
#define PARAM_TYPE_U16 0
#define PAR_PERMS_READ 1
#define MB_TCP 0
#define MB_IPV4 0
typedef struct {
    int cid; const char *param_key, *param_units;
    int mb_slave_addr, mb_param_type, mb_reg_start, mb_size;
    unsigned param_offset; int param_type, param_size;
    struct { int opt1, opt2, opt3; } param_opts;
    int access;
} mb_parameter_descriptor_t;
typedef struct {
    struct {
        int port, mode, addr_type; void *ip_addr_table; int uid;
        bool start_disconnected; int response_tout_ms; void *ip_netif_ptr;
    } tcp_opts;
} mb_communication_info_t;
typedef struct { int slave_addr, command, reg_start, reg_size; } mb_param_request_t;
int mbc_master_create_tcp(mb_communication_info_t *, void **);
int mbc_master_set_descriptor(void *, const mb_parameter_descriptor_t *, unsigned);
int mbc_master_start(void *);
int mbc_master_delete(void *);
int mbc_master_send_request(void *, mb_param_request_t *, void *);
#endif
