idf_component_register(
    SRCS "app_main.c" "sensors.c" "gpio_input.c"
    PRIV_REQUIRES esp-tls esp_wifi esp_event esp_driver_gpio esp_timer nvs_flash espressif__esp-modbus
    INCLUDE_DIRS "."
    EMBED_TXTFILES "mosquitto.org.crt"
)
