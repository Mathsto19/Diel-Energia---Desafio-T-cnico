# Registro de testes

Data do registro: 06/09/2026. Os sensores usados no modo `simulated` são valores gerados pelo firmware; não representam sensores físicos.

| Teste | Resultado observado |
| --- | --- |
| Inicialização | **Validado por compilação.** Não houve erro de alocação ou reinicialização durante a validação estática; execução em placa/Wokwi não foi realizada nesta sessão. |
| Telemetria | **Validado no código e na compilação.** O JSON inclui sensores, GPIO e diagnóstico; chegada pelo broker ainda precisa ser observada no MQTTX. |
| Pressionar e soltar | **Validado no código.** O componente Button registra eventos separados; latência local e chegada pelo broker não foram medidas nesta sessão. |
| Leitura Modbus demorada | **Não executado em hardware/Wokwi.** A consulta usa timeout e ocorre na tarefa de telemetria; a fila e a tarefa GPIO permanecem independentes. |
| Servidor Modbus desligado | **Validado por tratamento no código.** A leitura retorna inválida e a aplicação continua; falta evidência de monitor em execução. |
| Servidor restaurado | **Não executado.** O mestre repete a consulta no próximo período e deve recuperar os registradores. |
| Conexão do ESP32 interrompida | **Validado por configuração.** Outbox QoS 1 limitado a 16 KiB; não foi feita interrupção de rede durante uma execução. |
| Conexão restaurada | **Não executado.** O ESP-MQTT mantém retransmissão de pendências QoS 1. |
| Outbox cheio | **Validado no código.** Retornos de enfileiramento e descarte são contabilizados; não foi forçada uma fila cheia em execução. |
| Intervalo alterado | **Validado por compilação e código.** A fila de configuração aplica valores válidos e publica ACK; teste MQTTX de 5.000 para 2.000 ms pendente. |
| JSON de comando inválido | **Validado no código.** JSON inválido, tamanho excedente e intervalo fora da faixa são rejeitados sem reinicialização. |
| Certificado inválido | **Não executado nesta sessão.** A configuração usa bundle de CAs e validação do nome para rejeitar certificado/servidor incompatível. |
| Execução contínua | **Não executado.** Duração registrada: 0 minutos; é necessário repetir por pelo menos 30 minutos e anotar memória e reinicializações. |

## Evidências

Não foram salvas capturas do monitor serial ou do MQTTX nesta sessão. Para completar a entrega experimental, salvar capturas com `MQTT_EVENT_CONNECTED`, uma telemetria, um evento GPIO, um ACK de configuração e uma leitura Modbus inválida/restaurada.

A latência local deve ser registrada comparando o instante em milissegundos impresso no evento GPIO com o instante de chegada no MQTTX; esses valores devem ser anotados separadamente.
