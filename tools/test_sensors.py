"""Compila sensors.c real com dubles de FreeRTOS/ESP-Modbus; nao simula o Wokwi."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADERS = ["esp_err.h", "esp_check.h", "esp_log.h", "esp_modbus_master.h",
           "esp_timer.h", "protocol_examples_common.h", "freertos/FreeRTOS.h",
           "freertos/queue.h", "freertos/task.h", "lwip/inet.h", "lwip/netdb.h", "lwip/sockets.h"]


def main():
    import ziglang
    zig = Path(ziglang.__file__).parent / ("zig.exe" if os.name == "nt" else "zig")
    with tempfile.TemporaryDirectory(prefix="sensors-test-") as directory:
        tmp = Path(directory)
        for name in HEADERS:
            header = tmp / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text('#include "host_stubs.h"\n', encoding="utf-8")
        env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(tmp / "cache"))
        for mode in [1, 0]:
            executable = tmp / (f"test-{mode}" + (".exe" if os.name == "nt" else ""))
            command = [str(zig), "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unused-function", "-Wno-unused-variable",
                       f"-DCONFIG_EXAMPLE_SENSORS_MODBUS_TCP={mode}",
                       "-I", str(tmp), "-I", str(ROOT / "tools/tests"),
                       str(ROOT / "tools/tests/test_sensors.c"), "-o", str(executable)]
            subprocess.run(command, env=env, check=True, timeout=180)
            print(f"=== sensors.c: {'modbus_tcp' if mode else 'simulated'} ===", flush=True)
            subprocess.run([str(executable)], check=True, timeout=15)
    return 0


if __name__ == "__main__":
    sys.exit(main())
