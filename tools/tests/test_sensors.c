#include "host_stubs.h"
#include <setjmp.h>
#include "../../firmware/main/sensors.c"

static unsigned char queue_storage[128];
static unsigned queue_size;
static bool queue_full, fail_queue, fail_task;
static int64_t now_us;
static int dns_error, create_error, descriptor_error, start_error, delete_error, request_error;
static unsigned creates, deletes, requests, dns_calls;
static uint16_t temperature = 250, humidity = 600;
static void (*worker)(void *);
static jmp_buf iteration_done;
static bool inspect_during_request;

QueueHandle_t xQueueCreate(unsigned count, unsigned size) {
    assert(count == 1 && size <= sizeof(queue_storage));
    queue_size = size;
    return fail_queue ? NULL : queue_storage;
}
int xQueuePeek(QueueHandle_t q, void *out, unsigned wait) {
    assert(q == queue_storage && wait == 0); /* Contrato: consumidor nunca espera. */
    if (!queue_full) return pdFALSE;
    memcpy(out, queue_storage, queue_size);
    return pdTRUE;
}
int xQueueOverwrite(QueueHandle_t q, const void *in) {
    assert(q == queue_storage);
    memcpy(queue_storage, in, queue_size);
    queue_full = true;
    return pdTRUE;
}
void vQueueDelete(QueueHandle_t q) { assert(q == queue_storage); queue_full = false; }
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned prio, void *handle) {
    (void)name; (void)stack; (void)arg; (void)handle;
    assert(prio < 3);
    worker = fn;
    return fail_task ? pdFALSE : pdPASS;
}
void vTaskDelay(unsigned ticks) {
    assert(ticks == MODBUS_INTERVALO_CONSULTA_MS || ticks == MODBUS_INTERVALO_REPETICAO_MS);
    longjmp(iteration_done, 1);
}
int64_t esp_timer_get_time(void) { return now_us; }
int getaddrinfo(const char *host, const char *service, const struct addrinfo *hints, struct addrinfo **out) {
    static struct sockaddr_in addr;
    static struct addrinfo info;
    (void)host; (void)service; (void)hints;
    dns_calls++;
    info.ai_addr = &addr;
    *out = dns_error ? NULL : &info;
    return dns_error;
}
void freeaddrinfo(struct addrinfo *info) { (void)info; }
const char *inet_ntop(int family, const void *addr, char *out, size_t size) {
    (void)family; (void)addr;
    snprintf(out, size, "127.0.0.1");
    return out;
}
void *get_example_netif(void) { return (void *)1; }
int mbc_master_create_tcp(mb_communication_info_t *config, void **handle) {
    assert(config->tcp_opts.start_disconnected);
    assert(config->tcp_opts.response_tout_ms == 1000);
    creates++;
    *handle = create_error ? NULL : (void *)2;
    return create_error;
}
int mbc_master_set_descriptor(void *handle, const mb_parameter_descriptor_t *desc, unsigned count) {
    assert(handle && desc && count == 2);
    return descriptor_error;
}
int mbc_master_start(void *handle) { assert(handle); return start_error; }
int mbc_master_delete(void *handle) { assert(handle); deletes++; return delete_error; }
int mbc_master_send_request(void *handle, mb_param_request_t *req, void *out) {
    assert(handle && req->command == 3 && req->reg_start == 0 && req->reg_size == 2);
    assert(req->slave_addr == 1);
    requests++;
    if (inspect_during_request) {
        /* Simula uma operacao ainda pendente, sem permitir dados antigos validos. */
        now_us += 10000000;
        leitura_sensores_t leitura;
        assert(sensores_ler(&leitura) == ESP_OK && !leitura.leitura_valida);
    }
    ((uint16_t *)out)[0] = temperature;
    ((uint16_t *)out)[1] = humidity;
    return request_error;
}

#if CONFIG_EXAMPLE_SENSORS_MODBUS_TCP
static void step(void) { if (setjmp(iteration_done) == 0) worker(NULL); }
static void expect_read(bool valid, float temp, float hum) {
    unsigned before = requests;
    leitura_sensores_t leitura;
    assert(sensores_ler(&leitura) == ESP_OK);
    assert(requests == before); /* Ler a telemetria nao pode consultar a rede. */
    assert(leitura.leitura_valida == valid);
    assert(leitura.temperatura_celsius == temp && leitura.umidade_percentual == hum);
}
#endif

int main(void) {
    leitura_sensores_t leitura;
    assert(sensores_ler(NULL) == ESP_ERR_INVALID_ARG);
    assert(sensores_ler(&leitura) == ESP_ERR_INVALID_STATE);
#if CONFIG_EXAMPLE_SENSORS_MODBUS_TCP
    fail_queue = true;
    assert(sensores_iniciar() == ESP_ERR_NO_MEM);
    fail_queue = false; fail_task = true;
    assert(sensores_iniciar() == ESP_ERR_NO_MEM);
    fail_task = false;
    assert(sensores_iniciar() == ESP_OK && dns_calls == 0);
    assert(sensores_iniciar() == ESP_OK); /* Inicializacao idempotente. */
    expect_read(false, 0, 0);
    dns_error = 1; step(); expect_read(false, 0, 0);
    dns_error = 0; create_error = ESP_FAIL; step(); expect_read(false, 0, 0);
    create_error = 0; descriptor_error = ESP_FAIL; step(); assert(deletes == 1);
    descriptor_error = 0; start_error = ESP_FAIL; step(); assert(deletes == 2);
    start_error = 0; request_error = ESP_ERR_TIMEOUT; step(); expect_read(false, 0, 0);
    unsigned initial_creates = creates;
    request_error = 0; step(); expect_read(true, 25, 60);
    puts("PASS: inicializacao ausente, falhas de DNS/criacao/descritor/start e recuperacao");
    now_us += MODBUS_VALIDADE_LEITURA_MS * 1000 - 1;
    expect_read(true, 25, 60);
    now_us++; expect_read(false, 0, 0);
    step(); expect_read(true, 25, 60);
    request_error = ESP_ERR_TIMEOUT; inspect_during_request = true;
    step(); expect_read(false, 0, 0);
    puts("PASS: leitura nao bloqueia durante requisicao e amostra vencida fica invalida");
    inspect_during_request = false;
    for (int i = 0; i < 30; i++) { step(); expect_read(false, 0, 0); }
    temperature = 310; humidity = 725; request_error = 0;
    step(); expect_read(true, 31, 72.5f);
    assert(creates == initial_creates && deletes == 2);
    puts("PASS: 30 falhas e recuperacao com valores novos, sem recriar mestre/tarefa");
    modbus_inicializado = false;
    descriptor_error = ESP_FAIL; delete_error = ESP_FAIL;
    step(); unsigned before = creates; step(); assert(creates == before);
    expect_read(false, 0, 0);
    puts("PASS: falha de limpeza nao reutiliza objeto parcialmente destruido");
#else
    assert(sensores_iniciar() == ESP_OK);
    for (int i = 0; i < 40; i++) {
        assert(sensores_ler(&leitura) == ESP_OK && leitura.leitura_valida);
        assert(leitura.temperatura_celsius == 25.0f + (i % 20) * 0.2f);
        assert(leitura.umidade_percentual == 60.0f + (i % 20) * 0.5f);
    }
    puts("PASS: modo simulado mantem ciclo de 20 leituras");
#endif
    return 0;
}
