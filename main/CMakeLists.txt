set(MAIN_SRCS
    "main.cpp"
    "config_api.c"
    "coordinator_fallback.c"
    "ld2450_cli.c"
    "nvs_config.c"
    "zigbee_init.c"
    "zigbee_attr_handler.c"
    "sensor_bridge.c"
    "zigbee_signal_handlers.c"
)

if("${IDF_TARGET}" STREQUAL "esp32c6")
    list(APPEND MAIN_SRCS "wifi_manager.c" "web_server.c" "ota_check.c")
    set(C6_PRIV_REQUIRES esp_wifi esp_netif esp_event esp_http_server espressif__mdns esp_http_client mbedtls)
else()
    set(C6_PRIV_REQUIRES "")
endif()

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        nvs_flash
        json
        ${C6_PRIV_REQUIRES}
    REQUIRES
        ld2450
        espressif__esp-zigbee-lib
        espressif__esp-zboss-lib
        led_strip
        zigbee_ota
    WHOLE_ARCHIVE
)

if("${IDF_TARGET}" STREQUAL "esp32c6")
    # Web assets embedded directly in the firmware binary so OTA updates carry
    # the latest UI. The www SPIFFS partition is no longer mounted or used.
    target_add_binary_data(${COMPONENT_LIB} "../web/index.html" TEXT)
    target_add_binary_data(${COMPONENT_LIB} "../web/app.js"     TEXT)
    target_add_binary_data(${COMPONENT_LIB} "../web/style.css"  TEXT)
endif()
