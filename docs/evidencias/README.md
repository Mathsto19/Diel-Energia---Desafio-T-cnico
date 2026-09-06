# Evidências da entrega

Registro de 06/09/2026. Cada arquivo abaixo indica o que realmente comprova.

| Arquivo | Origem e alcance |
| --- | --- |
| [circuito.png](circuito.png) / [SVG editável](circuito.svg) | Esquema desenhado a partir de `firmware/diagram.json`; não é captura do simulador. |
| [funcionamento.png](funcionamento.png) / [SVG editável](funcionamento.svg) | Diagrama da arquitetura implementada; não é resultado de teste. |
| [build-modbus.txt](build-modbus.txt) | Saída da compilação ESP-IDF do firmware em modo Modbus TCP. |
| [build-simulated.txt](build-simulated.txt) | Saída da compilação ESP-IDF em diretório separado para o modo simulado. |
| [testes-sensores.txt](testes-sensores.txt) | Execução local de `sensors.c` com falhas injetadas por substitutos de FreeRTOS/DNS/ESP-Modbus. Não usa rede real. |

## Capturas do Wokwi

Capturas reais ainda não estavam disponíveis ao preparar este índice. Para completar a validação experimental, salvar aqui:

1. `01-circuito-wokwi.png`: circuito inteiro e simulação ativa.
2. `02-modbus-online.png`: telemetria com `sensors_valid=true`, `boot_id` e `uptime_ms`.
3. `03-modbus-offline.png`: pelo menos três telemetrias inválidas e evento do botão durante a queda.
4. `04-modbus-recuperado.png`: retorno de `sensors_valid=true` com o mesmo `boot_id` e uptime maior.
5. `05-mqtt-telemetria.png`: mensagem recebida no MQTTX, com tópico e JSON legíveis.
6. `wokwi-recuperacao.txt`: trecho contínuo do monitor, desde antes da queda até após a recuperação.

Os nomes são instruções para capturas futuras; não representam arquivos existentes. Ao adicionar os arquivos, atualizar este índice e a tabela de resultados em [`../testes.md`](../testes.md). Não incluir tokens, licenças, senhas ou telas de outras aplicações.
