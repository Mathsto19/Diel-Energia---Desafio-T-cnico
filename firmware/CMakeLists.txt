# Mantenha as instruções abaixo nesta ordem para o CMake funcionar corretamente.
cmake_minimum_required(VERSION 3.22)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Compila apenas o componente main e as dependências necessárias.
idf_build_set_property(MINIMAL_BUILD ON)
project(esp32_mqtt_telemetry)

# TODO IDF-15784: remover quando o problema da biblioteca MQTT for corrigido.
# A biblioteca MQTT atribui variáveis locais sem lê-las; suprime esse aviso do compilador.
idf_component_get_property(mqtt_lib espressif__mqtt COMPONENT_LIB)
target_compile_options(${mqtt_lib} PRIVATE -Wno-unused-but-set-variable)

