"""Servidor Modbus TCP simples para testar a telemetria do ESP32."""

import argparse
import time

from pyModbusTCP.server import ModbusServer


def principal() -> None:
    parser = argparse.ArgumentParser(description="Servidor Modbus TCP dos sensores")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=5020)
    argumentos = parser.parse_args()

    servidor = ModbusServer(host=argumentos.host, port=argumentos.port, no_block=True)
    # Offset zero: temperatura em decimos de grau; offset um: umidade em decimos de %.
    servidor.data_bank.set_holding_registers(0, [250, 600])
    servidor.start()
    print(f"Servidor Modbus TCP ouvindo em {argumentos.host}:{argumentos.port}")
    print("Registradores 0 e 1: temperatura=250 (25,0 C), umidade=600 (60,0 %)")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("Encerrando servidor Modbus TCP")
    finally:
        servidor.stop()


if __name__ == "__main__":
    principal()
