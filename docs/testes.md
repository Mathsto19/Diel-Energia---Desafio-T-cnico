# Registro de testes

Data do registro: 06/09/2026. Os sensores usados no modo `simulated` s�o valores gerados pelo firmware; n�o representam sensores f�sicos.

| Teste | Resultado observado |
| --- | --- |
| Inicializa��o | **Validado por compila��o.** N�o houve erro de aloca��o ou reinicializa��o durante a valida��o est�tica; execu��o em placa/Wokwi n�o foi realizada nesta sess�o. |
| Telemetria | **Validado no c�digo e na compila��o.** O JSON inclui sensores, GPIO e diagn�stico; chegada pelo broker ainda precisa ser observada no MQTTX. |
| Pressionar e soltar | **Validado em execução no Wokwi.** O GPIO 27 foi detectado em nível baixo ao pressionar e alto ao soltar, com debounce e geração dos eventos `pressionado` e `solto` no monitor serial. |
| Leitura Modbus demorada | **N�o executado em hardware/Wokwi.** A consulta usa timeout e ocorre na tarefa de telemetria; a fila e a tarefa GPIO permanecem independentes. |
| Servidor Modbus desligado | **Validado por tratamento no c�digo.** A leitura retorna inv�lida e a aplica��o continua; falta evid�ncia de monitor em execu��o. |
| Servidor restaurado | **N�o executado.** O mestre repete a consulta no pr�ximo per�odo e deve recuperar os registradores. |
| Conex�o do ESP32 interrompida | **Validado por configura��o.** Outbox QoS 1 limitado a 16 KiB; n�o foi feita interrup��o de rede durante uma execu��o. |
| Conex�o restaurada | **N�o executado.** O ESP-MQTT mant�m retransmiss�o de pend�ncias QoS 1. |
| Outbox cheio | **Validado no c�digo.** Retornos de enfileiramento e descarte s�o contabilizados; n�o foi for�ada uma fila cheia em execu��o. |
| Intervalo alterado | **Validado por compila��o e c�digo.** A fila de configura��o aplica valores v�lidos e publica ACK; teste MQTTX de 5.000 para 2.000 ms pendente. |
| JSON de comando inv�lido | **Validado no c�digo.** JSON inv�lido, tamanho excedente e intervalo fora da faixa s�o rejeitados sem reinicializa��o. |
| Certificado inv�lido | **N�o executado nesta sess�o.** A configura��o usa bundle de CAs e valida��o do nome para rejeitar certificado/servidor incompat�vel. |
| Execu��o cont�nua | **N�o executado.** Dura��o registrada: 0 minutos; � necess�rio repetir por pelo menos 30 minutos e anotar mem�ria e reinicializa��es. |

## Evid�ncias

N�o foram salvas capturas do monitor serial ou do MQTTX nesta sess�o. Para completar a entrega experimental, salvar capturas com `MQTT_EVENT_CONNECTED`, uma telemetria, um evento GPIO, um ACK de configura��o e uma leitura Modbus inv�lida/restaurada.

A lat�ncia local deve ser registrada comparando o instante em milissegundos impresso no evento GPIO com o instante de chegada no MQTTX; esses valores devem ser anotados separadamente.
