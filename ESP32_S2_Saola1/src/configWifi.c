#include <configWifi.h>

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const char *TAG = "smartconfig_example";

/* FreeRTOS event group to signal when we are connected & ready to make a request */
EventGroupHandle_t s_wifi_event_group;

void wifiConfigNVSConnect() {

    wifi_config_t *wifi_config;
    wifi_config = nvsReadBlob("wifi_settings", "wifi_config_t", sizeof(wifi_config_t)); // returns NULL if failed.

    // validate whether we successfully read the blob data.
    if (wifi_config) {
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_config));
        esp_wifi_connect();
    }
}

void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        xTaskCreate(smartconfig_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        printf("station connected!\n");
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        printf("station disconnected!\n");
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
        wifiConfigNVSConnect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);

        // update the current active configuration
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
        // attempt to connect to the AP
        esp_err_t err = esp_wifi_connect();
        // if connected to the AP OK, then write the new config to the NVS
        // Otherwise, report the error.
        if (err == ESP_OK) {
            // write to the nvs the wifi configuration
            nvsWriteBlob("wifi_settings", "wifi_config_t", &wifi_config, sizeof(wifi_config));
        } else {
            ESP_ERROR_CHECK(err);
        }
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

void smartconfig_task(void * parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}

// start the radio and attempt to connect with last known configuration
void initialize_wifi(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

    ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );

    // if successful or fail, an event will be raised in the event_handler callback.
    // if fails, it will automatically attempt to find the last known config in the NVS and try again.
    esp_wifi_connect();

}

void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
            xTaskCreate(smartconfig_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
        } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
            printf("station connected!\n");
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            printf("station disconnected!\n");
            xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
            wifiConfigNVSConnect();
        } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
            xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
        } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
            ESP_LOGI(TAG, "Scan done");
        } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
            ESP_LOGI(TAG, "Found channel");
        } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
            ESP_LOGI(TAG, "Got SSID and password");

            smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
            wifi_config_t wifi_config;
            uint8_t ssid[33] = { 0 };
            uint8_t password[65] = { 0 };
            uint8_t rvd_data[33] = { 0 };

            bzero(&wifi_config, sizeof(wifi_config_t));
            memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
            memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
            wifi_config.sta.bssid_set = evt->bssid_set;
            if (wifi_config.sta.bssid_set == true) {
                memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
            }

            memcpy(ssid, evt->ssid, sizeof(evt->ssid));
            memcpy(password, evt->password, sizeof(evt->password));
            ESP_LOGI(TAG, "SSID:%s", ssid);
            ESP_LOGI(TAG, "PASSWORD:%s", password);

            ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
            esp_wifi_connect();

            nvs_handle_t storage_handle;
            esp_err_t err = nvs_open("wifi_settings", NVS_READWRITE, &storage_handle);
            nvs_set_blob(storage_handle, "wifi_config_t", &wifi_config, sizeof(wifi_config));
            err = nvs_commit(storage_handle);
            nvs_close(storage_handle);

            vTaskDelay(pdMS_TO_TICKS(5000));
            ESP_ERROR_CHECK(esp_wifi_disconnect());

        } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
            xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
        }
}