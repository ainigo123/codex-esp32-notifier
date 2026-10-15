#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "audio_notify.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#define WIFI_CONNECTED_BIT BIT0
#define RX_BUFFER_SIZE 256

static const char *TAG = "codex_notifier";
static EventGroupHandle_t wifi_event_group;

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_init_sta(void)
{
    wifi_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(wifi_event_group != NULL, ESP_ERR_NO_MEM, TAG, "No event group memory");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop init failed");
    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_sta() != NULL, ESP_FAIL, TAG, "No STA netif");

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "Wi-Fi init failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL),
        TAG,
        "Wi-Fi handler registration failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL),
        TAG,
        "IP handler registration failed");

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", CONFIG_NOTIFY_WIFI_SSID);
    snprintf(
        (char *)wifi_config.sta.password,
        sizeof(wifi_config.sta.password),
        "%s",
        CONFIG_NOTIFY_WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Wi-Fi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "Wi-Fi config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start failed");

    ESP_LOGI(TAG, "Connecting to Wi-Fi");
    return ESP_OK;
}

static bool is_authorized_notification(const char *payload)
{
    char expected_prefix[160];
    const int length = snprintf(
        expected_prefix,
        sizeof(expected_prefix),
        "CODEX_DONE|%s|",
        CONFIG_NOTIFY_SHARED_TOKEN);

    return length > 0 &&
           (size_t)length < sizeof(expected_prefix) &&
           strncmp(payload, expected_prefix, (size_t)length) == 0;
}

static void udp_listener_task(void *arg)
{
    (void)arg;
    char rx_buffer[RX_BUFFER_SIZE];

    for (;;) {
        xEventGroupWaitBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT,
            pdFALSE,
            pdTRUE,
            portMAX_DELAY);

        const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "socket() failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        struct sockaddr_in listen_address = {
            .sin_family = AF_INET,
            .sin_port = htons(CONFIG_NOTIFY_UDP_PORT),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };

        if (bind(sock, (struct sockaddr *)&listen_address, sizeof(listen_address)) < 0) {
            ESP_LOGE(TAG, "bind() failed: errno %d", errno);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "Listening for Codex on UDP port %d", CONFIG_NOTIFY_UDP_PORT);

        for (;;) {
            struct sockaddr_storage source_address;
            socklen_t source_length = sizeof(source_address);
            const int length = recvfrom(
                sock,
                rx_buffer,
                sizeof(rx_buffer) - 1,
                0,
                (struct sockaddr *)&source_address,
                &source_length);

            if (length < 0) {
                ESP_LOGE(TAG, "recvfrom() failed: errno %d", errno);
                break;
            }

            rx_buffer[length] = '\0';
            if (is_authorized_notification(rx_buffer)) {
                ESP_LOGI(TAG, "Authorized Codex completion received");
                audio_notify_play();
            } else {
                ESP_LOGW(TAG, "Ignored unauthorized UDP packet");
            }
        }

        shutdown(sock, 0);
        close(sock);
    }
}

void app_main(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);

    if (strcmp(CONFIG_NOTIFY_WIFI_SSID, "") == 0) {
        ESP_LOGE(TAG, "Set the Wi-Fi SSID in menuconfig first");
        return;
    }
    if (strcmp(CONFIG_NOTIFY_SHARED_TOKEN, "change-me") == 0) {
        ESP_LOGE(TAG, "Change the shared token in menuconfig first");
        return;
    }

    ESP_ERROR_CHECK(audio_notify_init());
    ESP_ERROR_CHECK(wifi_init_sta());

    xTaskCreate(udp_listener_task, "udp_listener", 4096, NULL, 5, NULL);
}
