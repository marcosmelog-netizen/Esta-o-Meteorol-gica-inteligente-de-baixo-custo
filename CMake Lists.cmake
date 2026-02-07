cmake

include(pico_sdk_import.cmake)

project(estacao_meteo_iot C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Configurar FreeRTOS
set(FREERTOS_KERNEL_PATH ${PICO_SDK_PATH}/../FreeRTOS-Kernel)
include_directories(${FREERTOS_KERNEL_PATH}/include)
include_directories(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/include)
include_directories(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040)

add_executable(estacao_meteo_iot
    main.c
)

# Incluir todas bibliotecas necessárias
target_include_directories(estacao_meteo_iot PRIVATE
    ${PICO_SDK_PATH}/../lwip/src/include
    ${PICO_SDK_PATH}/../mbedtls/include
)

target_link_libraries(estacao_meteo_iot
    pico_stdlib
    pico_cyw43_arch
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
    lwip
    mbedtls
    mbedcrypto
    mbedx509
    FreeRTOS-Kernel
)

# Flags específicas para WiFi e TLS
target_compile_definitions(estacao_meteo_iot PRIVATE
    PICO_CYW43_ARCH_POLL=1
    LIB_PICO_MBEDTLS=1
    WIFI_SSID="${WIFI_SSID}"
    WIFI_PASSWORD="${WIFI_PASSWORD}"
)

pico_add_extra_outputs(estacao_meteo_iot)
pico_enable_stdio_usb(estacao_meteo_iot 1)

# Otimizações para tamanho
target_compile_options(estacao_meteo_iot PRIVATE
    -Os
    -Wno-unused-parameter
    -Wno-unused-variable

) 
