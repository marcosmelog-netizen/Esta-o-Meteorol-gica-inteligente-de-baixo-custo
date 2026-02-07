/**
 * ESTAÇÃO METEOROLÓGICA INTELIGENTE DE BAIXO CUSTO
 * Inclui: WiFi, MQTT com TLS/SSL, segurança, broker, app smartphone
  */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"

// MQTT e TLS
#include "lwip/apps/mqtt.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/certs.h"
#include "mbedtls/x509.h"
#include "mbedtls/error.h"
#include "mbedtls/debug.h"

// ============================================================================
// CONFIGURAÇÕES WIFI e MQTT (ALTERE AQUI!)
// ============================================================================

#define WIFI_SSID           "CIENTISTA"
#define WIFI_PASSWORD       "ONDAS DE PROBABILIDADE"

// Broker MQTT (HiveMQ público com TLS)
#define MQTT_BROKER         "broker.hivemq.com"
#define MQTT_PORT           8883  // Porta TLS
#define MQTT_CLIENT_ID      "estacao_meteo_01"

// Tópicos MQTT
#define TOPIC_TEMPERATURA   "estacao/temperatura"
#define TOPIC_UMIDADE       "estacao/umidade"
#define TOPIC_LUX           "estacao/luminosidade"
#define TOPIC_COR           "estacao/cor"
#define TOPIC_CHUVA         "estacao/chuva"
#define TOPIC_STATUS        "estacao/status"
#define TOPIC_COMANDOS      "estacao/comandos"

// Certificado TLS (HiveMQ público)
static const char mqtt_ca_cert[] = 
"-----BEGIN CERTIFICATE-----\n"
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"
"A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"
"T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"
"B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"
"B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"
"KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STGadaX0ZdAsSvG4Lxw\n"
"9LfPA3+f+5dR1Q==\n"
"-----END CERTIFICATE-----\n";

// ============================================================================
// DRIVERS DOS SENSORES (EMBUTIDOS)
// ============================================================================

// --- BME280 ---
#define BME280_ADDR          0x76
typedef struct {
    uint16_t dig_T1; int16_t dig_T2, dig_T3;
    uint16_t dig_P1; int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t dig_H1, dig_H3; int16_t dig_H2, dig_H4, dig_H5; int8_t dig_H6;
    int32_t t_fine;
} bme280_calib_data;
static bme280_calib_data bme280_calib;

bool bme280_init(i2c_inst_t *i2c, uint8_t addr) {
    uint8_t data[2] = {0xD0}; // REG_ID
    i2c_write_blocking(i2c, addr, data, 1, true);
    i2c_read_blocking(i2c, addr, data, 1, false);
    if (data[0] != 0x60) return false;
    
    data[0] = 0xE0; data[1] = 0xB6; // Reset
    i2c_write_blocking(i2c, addr, data, 2, false);
    sleep_ms(10);
    
    // Calibração (simplificada para exemplo)
    bme280_calib.dig_T1 = 27504; bme280_calib.dig_T2 = 26435; bme280_calib.dig_T3 = 1000;
    bme280_calib.dig_H1 = 75; bme280_calib.dig_H2 = 363; bme280_calib.dig_H3 = 0;
    bme280_calib.dig_H4 = 328; bme280_calib.dig_H5 = 50; bme280_calib.dig_H6 = 30;
    
    data[0] = 0xF2; data[1] = 0x01; // CTRL_HUM
    i2c_write_blocking(i2c, addr, data, 2, false);
    
    data[0] = 0xF4; data[1] = 0x27; // CTRL_MEAS
    i2c_write_blocking(i2c, addr, data, 2, false);
    
    sleep_ms(100);
    return true;
}

float bme280_read_temp(i2c_inst_t *i2c, uint8_t addr) {
    uint8_t data[8] = {0xF7};
    i2c_write_blocking(i2c, addr, data, 1, true);
    i2c_read_blocking(i2c, addr, data, 8, false);
    
    int32_t temp_raw = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);
    int32_t var1 = ((((temp_raw >> 3) - (bme280_calib.dig_T1 << 1))) * bme280_calib.dig_T2) >> 11;
    int32_t var2 = (((((temp_raw >> 4) - bme280_calib.dig_T1) * 
                     ((temp_raw >> 4) - bme280_calib.dig_T1)) >> 12) * bme280_calib.dig_T3) >> 14;
    bme280_calib.t_fine = var1 + var2;
    return (bme280_calib.t_fine * 5 + 128) >> 8 / 100.0f;
}

float bme280_read_hum(i2c_inst_t *i2c, uint8_t addr) {
    uint8_t data[2] = {0xFD}; // HUM_MSB
    i2c_write_blocking(i2c, addr, data, 1, true);
    i2c_read_blocking(i2c, addr, data, 2, false);
    
    int32_t hum_raw = (data[0] << 8) | data[1];
    int32_t v_x1_u32r = (bme280_calib.t_fine - 76800);
    v_x1_u32r = ((((hum_raw << 14) - (bme280_calib.dig_H4 << 20) - 
                  (bme280_calib.dig_H5 * v_x1_u32r)) + 16384) >> 15);
    v_x1_u32r = v_x1_u32r - ((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * 
                  bme280_calib.dig_H1 >> 4);
    v_x1_u32r = v_x1_u32r < 0 ? 0 : v_x1_u32r;
    v_x1_u32r = v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r;
    return (v_x1_u32r >> 12) / 1024.0f;
}

// --- BH1750 ---
#define BH1750_ADDR          0x23
float bh1750_read_lux(i2c_inst_t *i2c, uint8_t addr) {
    uint8_t cmd = 0x01; // Power ON
    i2c_write_blocking(i2c, addr, &cmd, 1, false);
    sleep_ms(10);
    
    cmd = 0x10; // Continuos H-Resolution
    i2c_write_blocking(i2c, addr, &cmd, 1, false);
    sleep_ms(180);
    
    uint8_t data[2];
    i2c_read_blocking(i2c, addr, data, 2, false);
    return ((data[0] << 8) | data[1]) / 1.2f;
}

// --- TCS34725 ---
#define TCS34725_ADDR        0x29
bool tcs34725_read_rgb(i2c_inst_t *i2c, uint8_t addr, uint16_t *r, uint16_t *g, uint16_t *b) {
    uint8_t data[8];
    data[0] = 0x80 | 0x20 | 0x14; // Command + Auto-increment + CDATAL
    i2c_write_blocking(i2c, addr, data, 1, true);
    
    if (i2c_read_blocking(i2c, addr, data, 8, false) != 8) return false;
    
    // Ignorar clear, ler apenas RGB
    if (r) *r = (data[3] << 8) | data[2];
    if (g) *g = (data[5] << 8) | data[4];
    if (b) *b = (data[7] << 8) | data[6];
    return true;
}

// --- VL53L0X ---
#define VL53L0X_ADDR         0x29
float vl53l0x_read_distance(i2c_inst_t *i2c, uint8_t addr) {
    uint8_t data[2] = {0x00}; // Start measurement
    i2c_write_blocking(i2c, addr, data, 1, false);
    data[0] = 0x01;
    i2c_write_blocking(i2c, addr, data, 1, false);
    
    sleep_ms(30);
    
    data[0] = 0x14; // Result range status
    i2c_write_blocking(i2c, addr, data, 1, true);
    i2c_read_blocking(i2c, addr, data, 2, false);
    
    float distance = (data[0] << 8) | data[1];
    return distance > 2000 ? 2000 : distance; // Limitar a 2m
}

// ============================================================================
// SISTEMA MQTT COM TLS/SSL
// ============================================================================

typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca_cert;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    struct tcp_pcb *tcp_pcb;
    ip_addr_t mqtt_broker_ip;
    bool connected;
    bool tls_ready;
} mqtt_client_t;

static mqtt_client_t mqtt_client;
static mqtt_connection_t mqtt_connection;
static char mqtt_rx_buffer[1024];
static char mqtt_tx_buffer[1024];

// Callback quando recebe mensagem MQTT
static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags) {
    printf("MQTT RX: %.*s\n", len, data);
    
    // Processar comandos do smartphone
    if (strstr((char*)data, "ABRIR_COBERTURA")) {
        printf("Comando: Abrir cobertura\n");
        // Acionar servo para abrir
    } else if (strstr((char*)data, "FECHAR_COBERTURA")) {
        printf("Comando: Fechar cobertura\n");
        // Acionar servo para fechar
    } else if (strstr((char*)data, "LEITURA_AGORA")) {
        printf("Comando: Leitura imediata\n");
        // Forçar leitura dos sensores
    }
}

// Callback de eventos MQTT
static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len) {
    printf("MQTT Publish: %s (len: %d)\n", topic, tot_len);
}

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
    if (status == MQTT_CONNECT_ACCEPTED) {
        printf("MQTT Conectado com sucesso!\n");
        mqtt_client.connected = true;
        
        // Inscrever nos tópicos de comandos
        mqtt_subscribe(&mqtt_connection, TOPIC_COMANDOS, 1, mqtt_incoming_publish_cb, NULL);
    } else {
        printf("MQTT Conexão recusada: %d\n", status);
        mqtt_client.connected = false;
    }
}

// Configurar TLS/SSL
static bool mqtt_tls_setup(void) {
    printf("Configurando TLS/SSL...\n");
    
    mbedtls_ssl_init(&mqtt_client.ssl);
    mbedtls_ssl_config_init(&mqtt_client.conf);
    mbedtls_x509_crt_init(&mqtt_client.ca_cert);
    mbedtls_entropy_init(&mqtt_client.entropy);
    mbedtls_ctr_drbg_init(&mqtt_client.ctr_drbg);
    
    // Seed random
    const char *pers = "estacao_meteo_tls";
    if (mbedtls_ctr_drbg_seed(&mqtt_client.ctr_drbg, mbedtls_entropy_func, 
                              &mqtt_client.entropy, (const unsigned char*)pers, strlen(pers)) != 0) {
        printf("Erro seed TLS\n");
        return false;
    }
    
    // Carregar certificado CA
    if (mbedtls_x509_crt_parse(&mqtt_client.ca_cert, (const unsigned char*)mqtt_ca_cert, 
                               strlen(mqtt_ca_cert) + 1) != 0) {
        printf("Erro carregar certificado CA\n");
        return false;
    }
    
    // Configurar SSL
    if (mbedtls_ssl_config_defaults(&mqtt_client.conf, MBEDTLS_SSL_IS_CLIENT,
                                   MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        printf("Erro config SSL\n");
        return false;
    }
    
    mbedtls_ssl_conf_authmode(&mqtt_client.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&mqtt_client.conf, &mqtt_client.ca_cert, NULL);
    mbedtls_ssl_conf_rng(&mqtt_client.conf, mbedtls_ctr_drbg_random, &mqtt_client.ctr_drbg);
    
    if (mbedtls_ssl_setup(&mqtt_client.ssl, &mqtt_client.conf) != 0) {
        printf("Erro setup SSL\n");
        return false;
    }
    
    // Configurar hostname (necessário para verificação do certificado)
    if (mbedtls_ssl_set_hostname(&mqtt_client.ssl, MQTT_BROKER) != 0) {
        printf("Erro set hostname SSL\n");
        return false;
    }
    
    mqtt_client.tls_ready = true;
    printf("TLS/SSL configurado com sucesso!\n");
    return true;
}

// Conectar ao broker MQTT com TLS
static bool mqtt_connect(void) {
    printf("Conectando ao broker MQTT %s:%d...\n", MQTT_BROKER, MQTT_PORT);
    
    // Resolver DNS
    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(MQTT_BROKER, &mqtt_client.mqtt_broker_ip, 
                                  NULL, NULL);
    cyw43_arch_lwip_end();
    
    if (err != ERR_OK && mqtt_client.mqtt_broker_ip.addr == 0) {
        printf("Erro DNS: %d\n", err);
        return false;
    }
    
    printf("Broker IP: %s\n", ipaddr_ntoa(&mqtt_client.mqtt_broker_ip));
    
    // Criar conexão TCP
    mqtt_client.tcp_pcb = tcp_new();
    if (!mqtt_client.tcp_pcb) {
        printf("Erro criar TCP PCB\n");
        return false;
    }
    
    // Configurar callback MQTT
    mqtt_connection.client = &mqtt_client;
    mqtt_connection.tcp = mqtt_client.tcp_pcb;
    mqtt_connection.rx_buffer = mqtt_rx_buffer;
    mqtt_connection.rx_buffer_size = sizeof(mqtt_rx_buffer);
    mqtt_connection.tx_buffer = mqtt_tx_buffer;
    mqtt_connection.tx_buffer_size = sizeof(mqtt_tx_buffer);
    mqtt_connection.callback = mqtt_connection_cb;
    mqtt_connection.arg = NULL;
    
    // Conectar TCP
    cyw43_arch_lwip_begin();
    err = tcp_connect(mqtt_client.tcp_pcb, &mqtt_client.mqtt_broker_ip, 
                      MQTT_PORT, mqtt_tcp_connect_cb);
    cyw43_arch_lwip_end();
    
    if (err != ERR_OK) {
        printf("Erro conectar TCP: %d\n", err);
        return false;
    }
    
    printf("Conexão TCP estabelecida. Configurando TLS...\n");
    
    // Configurar socket para TLS
    mbedtls_ssl_set_bio(&mqtt_client.ssl, mqtt_client.tcp_pcb,
                       mbedtls_net_send, mbedtls_net_recv, NULL);
    
    // Handshake TLS
    printf("Realizando handshake TLS...\n");
    int ret;
    while ((ret = mbedtls_ssl_handshake(&mqtt_client.ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            printf("Erro handshake TLS: %d\n", ret);
            return false;
        }
        sleep_ms(100);
    }
    
    printf("Handshake TLS completo! Conectando MQTT...\n");
    
    // Conectar MQTT sobre TLS
    mqtt_connect_client(&mqtt_connection, MQTT_CLIENT_ID, NULL, NULL, 0, NULL, NULL, 0);
    
    return true;
}

// Publicar dados MQTT
static bool mqtt_publish(const char *topic, const char *message, uint8_t qos) {
    if (!mqtt_client.connected) {
        printf("MQTT não conectado\n");
        return false;
    }
    
    printf("MQTT Publicando: %s -> %s\n", topic, message);
    
    cyw43_arch_lwip_begin();
    err_t err = mqtt_publish(&mqtt_connection, topic, message, strlen(message), qos, 0, NULL, NULL);
    cyw43_arch_lwip_end();
    
    return (err == ERR_OK);
}

// ============================================================================
// SISTEMA WIFI
// ============================================================================

static bool wifi_connect(void) {
    printf("Conectando WiFi: %s\n", WIFI_SSID);
    
    if (cyw43_arch_init()) {
        printf("Erro init WiFi\n");
        return false;
    }
    
    cyw43_arch_enable_sta_mode();
    
    printf("Scaneando redes...\n");
    int scan_result = cyw43_wifi_scan(&cyw43_state, NULL, CYW43_SCAN_ACTIVE, 5000);
    if (scan_result != 0) {
        printf("Erro scan: %d\n", scan_result);
    }
    
    printf("Conectando...\n");
    int conn_result = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, 
                                                         CYW43_AUTH_WPA2_AES_PSK, 10000);
    if (conn_result != 0) {
        printf("Erro conexão WiFi: %d\n", conn_result);
        return false;
    }
    
    printf("WiFi conectado!\n");
    
    // Obter IP
    struct netif *nif = cyw43_arch_netif();
    printf("IP: %s\n", ip4addr_ntoa(netif_ip4_addr(nif)));
    printf("Gateway: %s\n", ip4addr_ntoa(netif_ip4_gw(nif)));
    printf("Netmask: %s\n", ip4addr_ntoa(netif_ip4_netmask(nif)));
    
    return true;
}

// ============================================================================
// ESTRUTURA DO SISTEMA COMPLETO
// ============================================================================

#define I2C_PORT            i2c0
#define I2C_SDA_PIN         4
#define I2C_SCL_PIN         5
#define SERVO_PIN           16
#define BUZZER_PIN          14
#define LED_PIN             6

typedef struct {
    float temperatura;
    float umidade;
    float luminosidade;
    uint16_t r, g, b;
    float distancia;
    uint8_t chuva_detectada;
    uint32_t timestamp;
} SensorData;

// Variáveis RTOS
static TaskHandle_t task_sensores = NULL;
static TaskHandle_t task_mqtt = NULL;
static TaskHandle_t task_servo = NULL;
static TaskHandle_t task_interface = NULL;
static QueueHandle_t fila_dados = NULL;
static EventGroupHandle_t eventos = NULL;
static SemaphoreHandle_t mutex_i2c = NULL;

// Eventos
#define EVENT_CHUVA_DETECTADA   (1 << 0)
#define EVENT_WIFI_CONECTADO    (1 << 1)
#define EVENT_MQTT_CONECTADO    (1 << 2)
#define EVENT_ENVIAR_DADOS      (1 << 3)

// ============================================================================
// TAREFAS PRINCIPAIS
// ============================================================================

// Tarefa: Leitura de sensores
void tarefa_sensores(void *pv) {
    SensorData dados;
    
    // Inicializar I2C
    i2c_init(I2C_PORT, 400000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    
    // Inicializar sensores
    bme280_init(I2C_PORT, BME280_ADDR);
    
    while (1) {
        xSemaphoreTake(mutex_i2c, portMAX_DELAY);
        
        // Ler todos os sensores
        dados.temperatura = bme280_read_temp(I2C_PORT, BME280_ADDR);
        dados.umidade = bme280_read_hum(I2C_PORT, BME280_ADDR);
        dados.luminosidade = bh1750_read_lux(I2C_PORT, BH1750_ADDR);
        tcs34725_read_rgb(I2C_PORT, TCS34725_ADDR, &dados.r, &dados.g, &dados.b);
        dados.distancia = vl53l0x_read_distance(I2C_PORT, VL53L0X_ADDR);
        
        // Detectar chuva (distância < 10mm)
        dados.chuva_detectada = (dados.distancia < 10.0f) ? 1 : 0;
        if (dados.chuva_detectada) {
            xEventGroupSetBits(eventos, EVENT_CHUVA_DETECTADA);
        }
        
        dados.timestamp = to_ms_since_boot(get_absolute_time());
        
        xSemaphoreGive(mutex_i2c);
        
        // Enviar para fila
        xQueueSend(fila_dados, &dados, portMAX_DELAY);
        
        // Log
        printf("Sensores: %.1fC | %.1f%% | %.0flux | RGB(%d,%d,%d) | Dist:%.0fmm | %s\n",
               dados.temperatura, dados.umidade, dados.luminosidade,
               dados.r, dados.g, dados.b, dados.distancia,
               dados.chuva_detectada ? "CHUVA!" : "Normal");
        
        vTaskDelay(pdMS_TO_TICKS(5000)); // 5 segundos
    }
}

// Tarefa: Comunicação MQTT
void tarefa_mqtt(void *pv) {
    SensorData dados;
    char payload[256];
    
    printf("Tarefa MQTT iniciando...\n");
    
    // Conectar WiFi
    if (!wifi_connect()) {
        printf("Falha WiFi. Reiniciando em 10s...\n");
        vTaskDelay(pdMS_TO_TICKS(10000));
        NVIC_SystemReset();
    }
    xEventGroupSetBits(eventos, EVENT_WIFI_CONECTADO);
    
    // Configurar TLS
    if (!mqtt_tls_setup()) {
        printf("Falha TLS. Continuando sem segurança...\n");
    }
    
    // Conectar MQTT
    if (!mqtt_connect()) {
        printf("Falha MQTT. Tentando novamente...\n");
    } else {
        xEventGroupSetBits(eventos, EVENT_MQTT_CONECTADO);
    }
    
    while (1) {
        if (xQueueReceive(fila_dados, &dados, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // Enviar dados via MQTT
            snprintf(payload, sizeof(payload), 
                    "{\"temp\":%.1f,\"umid\":%.1f,\"lux\":%.0f,\"dist\":%.0f,\"chuva\":%d,\"timestamp\":%lu}",
                    dados.temperatura, dados.umidade, dados.luminosidade,
                    dados.distancia, dados.chuva_detectada, dados.timestamp);
            
            if (mqtt_client.connected) {
                mqtt_publish(TOPIC_TEMPERATURA, payload, 1);
                
                // Enviar status
                snprintf(payload, sizeof(payload), "{\"status\":\"OK\",\"wifi\":1,\"mqtt\":1}");
                mqtt_publish(TOPIC_STATUS, payload, 0);
            }
        }
        
        // Manter conexão ativa
        if (!mqtt_client.connected) {
            printf("Reconectando MQTT...\n");
            mqtt_connect();
        }
        
        vTaskDelay(pdMS_TO_TICKS(30000)); // 30 segundos
    }
}

// Tarefa: Controle do servo (proteção contra chuva)
void tarefa_servo(void *pv) {
    gpio_set_function(SERVO_PIN, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(SERVO_PIN);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 64.0f);
    pwm_config_set_wrap(&cfg, 19500);
    pwm_init(slice, &cfg, true);
    
    bool coberta = false;
    
    while (1) {
        EventBits_t bits = xEventGroupGetBits(eventos);
        
        if ((bits & EVENT_CHUVA_DETECTADA) && !coberta) {
            printf("Ativando proteção contra chuva!\n");
            // Servo para 90° (cobrir)
            pwm_set_gpio_level(SERVO_PIN, 1500);
            coberta = true;
            
            // Enviar alerta MQTT
            mqtt_publish(TOPIC_CHUVA, "{\"alerta\":\"CHUVA_DETECTADA\",\"acao\":\"COBERTURA_ATIVADA\"}", 2);
            
            xEventGroupClearBits(eventos, EVENT_CHUVA_DETECTADA);
        }
        else if (!(bits & EVENT_CHUVA_DETECTADA) && coberta) {
            printf("Desativando proteção\n");
            // Servo para 0° (abrir)
            pwm_set_gpio_level(SERVO_PIN, 500);
            coberta = false;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Tarefa: Interface local (OLED e LEDs)
void tarefa_interface(void *pv) {
    SensorData dados;
    uint32_t last_display = 0;
    
    // Configurar LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    
    while (1) {
        // Piscar LED se MQTT conectado
        EventBits_t bits = xEventGroupGetBits(eventos);
        static bool led_state = false;
        
        if (bits & EVENT_MQTT_CONECTADO) {
            led_state = !led_state;
            gpio_put(LED_PIN, led_state);
        } else {
            gpio_put(LED_PIN, 0);
        }
        
        // Atualizar display a cada 2 segundos
        if (xTaskGetTickCount() - last_display > pdMS_TO_TICKS(2000)) {
            if (xQueuePeek(fila_dados, &dados, 0) == pdTRUE) {
                printf("\n=== DISPLAY ===\n");
                printf("Temp: %.1f C\n", dados.temperatura);
                printf("Umid: %.1f %%\n", dados.umidade);
                printf("Luz:  %.0f lux\n", dados.luminosidade);
                printf("Dist: %.0f mm\n", dados.distancia);
                printf("Status: %s\n", dados.chuva_detectada ? "CHUVA!" : 
                                      (bits & EVENT_MQTT_CONECTADO) ? "MQTT OK" : "OFFLINE");
                printf("================\n");
            }
            last_display = xTaskGetTickCount();
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ============================================================================
// MAIN - PONTO DE ENTRADA
// ============================================================================

int main() {
    stdio_init_all();
    printf("\n\n=== ESTAÇÃO METEOROLÓGICA IoT ===\n");
    printf("Inicializando sistema...\n");
    
    // Criar recursos RTOS
    fila_dados = xQueueCreate(10, sizeof(SensorData));
    eventos = xEventGroupCreate();
    mutex_i2c = xSemaphoreCreateMutex();
    
    // Criar tarefas
    xTaskCreate(tarefa_sensores, "SENSORES", 2048, NULL, 3, &task_sensores);
    xTaskCreate(tarefa_mqtt, "MQTT", 4096, NULL, 3, &task_mqtt);
    xTaskCreate(tarefa_servo, "SERVO", 1024, NULL, 2, &task_servo);
    xTaskCreate(tarefa_interface, "INTERFACE", 1024, NULL, 1, &task_interface);
    
    printf("Sistema inicializado. Iniciando tarefas...\n");
    printf("Conectando WiFi: %s\n", WIFI_SSID);
    printf("Broker MQTT: %s:%d\n", MQTT_BROKER, MQTT_PORT);
    printf("Tópicos: estacao/[temperatura|umidade|luminosidade|cor|chuva|status|comandos]\n");
    
    vTaskStartScheduler();
    
    while (1) {
        sleep_ms(1000);
    }
    
    return 0;
}

