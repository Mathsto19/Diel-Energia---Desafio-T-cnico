# Servidor Modbus TCP de teste

O servidor de teste usa os registradores Holding Register a partir do offset zero:

- `0`: temperatura em décimos de grau (`250` = `25,0 °C`)
- `1`: umidade em décimos de percentual (`600` = `60,0%`)

Para preparar e executar:

```powershell
py -m venv tools\.venv
.\tools\.venv\Scripts\python.exe -m pip install pyModbusTCP==0.3.0
.\tools\.venv\Scripts\python.exe tools\modbus_server.py --host 0.0.0.0 --port 5020
```

O modo `modbus_tcp` do firmware usa `host.wokwi.internal` por padrão, adequado ao Wokwi. Em uma placa física, altere `MODBUS_TCP_HOST` em `firmware/main/app_config.h` para o IP do computador e habilite `Usar sensores Modbus TCP` no `menuconfig`. Se o servidor não responder, a telemetria continua sendo publicada com `sensors_valid: false`.

## Regressão local dos sensores

```powershell
py -m venv tools\.venv-test
.\tools\.venv-test\Scripts\python.exe -m pip install ziglang==0.14.1
.\tools\.venv-test\Scripts\python.exe tools\test_sensors.py
```

O Zig fornece o compilador C para executar o próprio `firmware/main/sensors.c` no computador. Os arquivos em `tools/tests/` substituem as dependências ESP-IDF por falhas controladas. São exercitados DNS indisponível, falhas de criação/configuração/início, leitura demorada, expiração da amostra, 30 falhas seguidas, recuperação com valores novos e modo simulado. Isso não substitui o teste do driver real no Wokwi descrito em [`docs/testes.md`](../docs/testes.md).
