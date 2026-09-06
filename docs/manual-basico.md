# Manual básico de uso

## Para que serve

O ESP32 lê temperatura e umidade e envia os dados pela internet para um servidor MQTT (broker). O MQTTX no computador recebe essas mensagens. O Wokwi simula o ESP32 e seu botão.

No modo inicial, `simulated`, as medições são geradas pelo firmware: não representam a temperatura ou a umidade do ambiente. Não é necessário iniciar o servidor Modbus para esse modo.

## 1. Compilar

Na instalação local deste projeto, execute no PowerShell:

```powershell
Set-Location C:\ProjetosESP\Diel-Energia---Desafio-T-cnico
powershell -NoProfile -ExecutionPolicy Bypass -File .\compilar.ps1
```

Espere `Project build complete`. O script configura o ambiente ESP-IDF 6.1 instalado neste computador e compila dentro de `firmware`. Seus caminhos são específicos desta instalação. Em outro computador, use o terminal ESP-IDF e as instruções do [README](../README.md).

## 2. Iniciar o Wokwi

No VS Code, abra `firmware/diagram.json`, pressione `Ctrl+Shift+P` e execute **Wokwi: Start Simulator**. Se a extensão não encontrar a configuração, abra a pasta `firmware` como pasta do VS Code.

O monitor deve mostrar a inicialização, a conexão Wi-Fi, a sincronização do relógio, a validação do certificado e `MQTT_EVENT_CONNECTED`. Depois aparecem as leituras, inicialmente a cada 5 segundos.

## 3. Entender o monitor

Exemplo:

```text
I (88177) telemetria: Telemetria: 27.6 C | 66.5 % | modo=simulated | intervalo=5000 ms
```

- `I`: registro informativo.
- `(88177)`: marca de tempo do registro, em milissegundos desde o início do sistema.
- Primeiro `telemetria:`: nome do módulo que escreveu o registro.
- Segundo `Telemetria:`: texto da mensagem que descreve a leitura.
- `27.6 C` e `66.5 %`: temperatura e umidade simuladas.
- `5000 ms`: intervalo configurado de 5 segundos.

A repetição da palavra não significa dois pacotes. Essa linha é um registro local; a recepção pelo broker deve ser conferida no MQTTX.

## 4. Usar o botão

O botão representa uma entrada digital, como um contato de porta ou interruptor de uma máquina. Ele não liga nem desliga os sensores.

| Ação | Nível do GPIO 27 | Resultado esperado |
| --- | --- | --- |
| Deixar solto | 1 | Estado de repouso |
| Pressionar | 0 | Produzir evento de pressionamento |
| Soltar | 1 | Produzir evento de soltura |

Pressione e segure o botão azul por dois segundos, depois solte. Cada mudança estável produz uma mensagem separada em `/event`, sem aguardar o intervalo periódico. A chegada depende da conexão e das mensagens pendentes. O estado atual também acompanha a telemetria periódica.

## 5. Ver as mensagens no MQTTX

Use o aplicativo MQTTX para computador. Crie uma conexão com estes valores:

| Campo | Valor |
| --- | --- |
| Nome | Teste ESP32 |
| Protocolo | mqtts:// |
| Host | test.mosquitto.org |
| Porta | 8886 |
| Client ID | Gere um identificador diferente do ESP32 |
| Usuário e senha | Vazios |
| SSL/TLS | Ativado |
| Certificado | CA signed server |
| Validação do certificado e nome do servidor | Ativada |

Clique em **Connect**. Em **New Subscription**, assine este tópico com QoS 1:

```text
mathsto19/desafio/esp32-01-7c91/#
```

O `#` permite receber os diferentes tópicos desse dispositivo. A configuração de conexão e assinatura segue o [manual oficial do MQTTX](https://mqttx.app/docs/get-started). A porta TLS é documentada pelo [broker Mosquitto](https://test.mosquitto.org/).

| Final do tópico | Conteúdo |
| --- | --- |
| `/telemetry` | Temperatura, umidade, GPIO e diagnóstico periódicos |
| `/event` | Mudanças do botão |
| `/config/set` | Comando enviado pelo computador |
| `/config/ack` | Resposta da aplicação ao comando |

No JSON, `temperature_c` é temperatura, `humidity_pct` é umidade, `gpio_level` é o estado do botão e `sensors_valid` informa se a leitura é válida. `boot_id` identifica a inicialização e `seq` identifica a sequência das mensagens.

## 6. Alterar o intervalo

No MQTTX, publique no tópico:

```text
mathsto19/desafio/esp32-01-7c91/config/set
```

Selecione JSON, QoS 1 e Retain desativado. Envie:

```json
{"request_id":"teste-01","telemetry_interval_ms":2000}
```

Confira a resposta em `/config/ack` e as novas leituras com intervalo de 2 segundos. São aceitos valores inteiros de 1000 a 60000 milissegundos. Para voltar a 5 segundos, envie outro comando com `5000` e um novo `request_id`.

## 7. Encerrar e próximos testes

Pare o Wokwi pelo botão de parar da simulação. Fechar o MQTTX apenas fecha o observador: não desconecta o ESP32 do broker. Reiniciar o ESP32 perde as mensagens pendentes em RAM; QoS 1 pode gerar duplicatas.

Para consultar registradores Modbus em vez dos valores internos, siga a seção Modbus do [README](../README.md) e as [instruções do simulador](../tools/README.md). Esse teste exige configurar o modo Modbus e a comunicação de rede com o computador.

A compilação e o registro enviado pelo usuário confirmaram inicialização, conexão MQTT e leituras simuladas. Os testes de recepção no MQTTX, botão e estabilidade prolongada devem ser conferidos e registrados em [testes.md](testes.md); este manual não os considera concluídos automaticamente.