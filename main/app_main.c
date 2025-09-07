#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "mdns.h"
#include "esp_qrcode.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

static const char *TAG = "CA360_ESP32";

#define WIFI_SSID      "CA360_Device"
#define WIFI_PASS      "ca360123"
#define WIFI_CHANNEL   1
#define MAX_STA_CONN   4

// mDNS service configuration
#define MDNS_HOSTNAME  "ca360-esp32"
#define MDNS_INSTANCE  "CA360 IoT Device"

// HTTP server configuration
#define HTTP_PORT      80

// LED Blink GPIO
#define BLINK_GPIO 2

static EventGroupHandle_t s_wifi_event_group;
static httpd_handle_t server = NULL;

/* ---------------- LED Blinking Task ---------------- */
void blink_task(void *pvParameter) {
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    while (1) {
        gpio_set_level(BLINK_GPIO, 1);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        gpio_set_level(BLINK_GPIO, 0);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

/* ---------------- /info Endpoint ---------------- */
esp_err_t info_get_handler(httpd_req_t *req) {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    char resp[256];
    snprintf(resp, sizeof(resp),
        "{ \"chip_revision\": %d, \"cores\": %d, \"heap_free\": %d, \"uptime_ms\": %lld }",
        chip_info.revision, chip_info.cores,
        esp_get_free_heap_size(),
        esp_timer_get_time() / 1000);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

httpd_uri_t info_uri = {
    .uri       = "/info",
    .method    = HTTP_GET,
    .handler   = info_get_handler,
    .user_ctx  = NULL
};


esp_err_t clients_get_handler(httpd_req_t *req) {
    wifi_sta_list_t sta_list;
    esp_wifi_ap_get_sta_list(&sta_list);

    char resp[64];
    snprintf(resp, sizeof(resp),
        "{ \"connected_clients\": %d }", sta_list.num);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

httpd_uri_t clients_uri = {
    .uri       = "/clients",
    .method    = HTTP_GET,
    .handler   = clients_get_handler,
    .user_ctx  = NULL
};

/* ---------------- Wi-Fi and Server Init ---------------- */
static void wifi_init_softap(void) {
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .password = WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "WiFi AP started. SSID:%s password:%s channel:%d",
             WIFI_SSID, WIFI_PASS, WIFI_CHANNEL);
}

static httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HTTP_PORT;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &info_uri);
        httpd_register_uri_handler(server, &clients_uri);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

/* ---------------- Main Entry ---------------- */
void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_softap();


    start_webserver();


    xTaskCreate(&blink_task, "blink_task", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "System ready. Access /info or /clients via browser.");
}