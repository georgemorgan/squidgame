/* ESPNOW Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
   This example shows how to use ESPNOW.
   Prepare two device, one for sending ESPNOW data and another for receiving
   ESPNOW data.
*/
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "esp_event_loop.h"
#include "tcpip_adapter.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_now.h"
#include "rom/ets_sys.h"
#include "rom/crc.h"
#include "espnow_example.h"
#include "driver/gpio.h"
#include "driver/hw_timer.h"
#include "driver/uart.h"
#include "esp_spiffs.h"

static const char *TAG = "beast_squib";

static xQueueHandle beastsquib_espnow_queue;

static uint8_t beastsquib_broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint16_t s_beastsquib_espnow_seq[beastsquib_ESPNOW_DATA_MAX] = { 0, 0 };

static void beastsquib_espnow_deinit(beastsquib_espnow_send_param_t *send_param);

// #define TX

// TX
#ifdef TX
#define GPIO_OUTPUT_IO_16    5
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_16))
#else
// RX
#define GPIO_OUTPUT_BOOM     15
#define GPIO_OUTPUT_LED      16
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_BOOM) | (1ULL<<GPIO_OUTPUT_LED))
#endif

static uint16_t ticks_since_last_packet = 0;

/* WiFi should start before using ESPNOW */
static void beastsquib_wifi_init(void)
{
    tcpip_adapter_init();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(ESPNOW_WIFI_MODE) );
    // Max power
    esp_wifi_set_max_tx_power(84);
    // ESP_ERROR_CHECK ( esp_wifi_config_espnow_rate(ESPNOW_WIFI_MODE, 0x10) );
    ESP_ERROR_CHECK( esp_wifi_start());

    /* In order to simplify example, channel is set after WiFi started.
     * This is not necessary in real application if the two devices have
     * been already on the same channel.
     */
    ESP_ERROR_CHECK( esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, 0) );
}

/* ESPNOW sending or receiving callback function is called in WiFi task.
 * Users should not do lengthy operations from this task. Instead, post
 * necessary data to a queue and handle it from a lower priority task. */
static void beastsquib_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    beastsquib_espnow_event_t evt;
    beastsquib_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    evt.id = BEASTSQUIB_ESPNOW_SEND_CB;
    memcpy(send_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;
    if (xQueueSend(beastsquib_espnow_queue, &evt, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send send queue fail");
    }
}

static void beastsquib_espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    beastsquib_espnow_event_t evt;
    beastsquib_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

    if (mac_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    evt.id = BEASTSQUIB_ESPNOW_RECV_CB;
    memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL) {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;
    if (xQueueSend(beastsquib_espnow_queue, &evt, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send receive queue fail");
        free(recv_cb->data);
    }
}

/* Parse received ESPNOW data. */
int beastsquib_espnow_data_parse(uint8_t *data, uint16_t data_len, uint8_t *state, uint16_t *seq, int *magic)
{
    beastsquib_espnow_data_t *buf = (beastsquib_espnow_data_t *)data;
    uint16_t crc, crc_cal = 0;

    if (data_len < sizeof(beastsquib_espnow_data_t)) {
        ESP_LOGE(TAG, "Receive ESPNOW data too short, len:%d", data_len);
        return -1;
    }

    *state = buf->state;
    *seq = buf->seq_num;
    *magic = buf->magic;
    crc = buf->crc;
    buf->crc = 0;
    crc_cal = crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len);

    if (crc_cal == crc) {
        return buf->type;
    }

    return -1;
}

/* Prepare ESPNOW data to be sent. */
void beastsquib_espnow_data_prepare(beastsquib_espnow_send_param_t *send_param)
{
    beastsquib_espnow_data_t *buf = (beastsquib_espnow_data_t *)send_param->buffer;
    int i = 0;

    assert(send_param->len >= sizeof(beastsquib_espnow_data_t));

    buf->type = IS_BROADCAST_ADDR(send_param->dest_mac) ? beastsquib_ESPNOW_DATA_BROADCAST : beastsquib_ESPNOW_DATA_UNICAST;
    buf->state = send_param->state;
    buf->seq_num = s_beastsquib_espnow_seq[buf->type]++;
    buf->crc = 0;
    buf->magic = send_param->magic;
    for (i = 0; i < send_param->len - sizeof(beastsquib_espnow_data_t); i++) {
        buf->payload[i] = (uint8_t)esp_random();
    }
    buf->crc = crc16_le(UINT16_MAX, (uint8_t const *)buf, send_param->len);
}

static void beastsquib_espnow_task(void *pvParameter)
{
    beastsquib_espnow_event_t evt;

    while (xQueueReceive(beastsquib_espnow_queue, &evt, portMAX_DELAY) == pdTRUE) {
        switch (evt.id) {
            case BEASTSQUIB_ESPNOW_SEND_CB:
            {

                break;
            }
            case BEASTSQUIB_ESPNOW_RECV_CB:
            {
                beastsquib_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
                free(recv_cb->data);

                ticks_since_last_packet = 0;

                // boom
                gpio_set_level(GPIO_OUTPUT_LED, 0);
                gpio_set_level(GPIO_OUTPUT_BOOM, 1);

                // ESP_LOGI(TAG, "recieved data");

                break;
            }
            default:
                ESP_LOGE(TAG, "Callback type error: %d", evt.id);
                break;
        }
    }
}

static void tx_transmit_task(void *pvParameter)
{
    while (1)
    {
        vTaskDelay(5 / portTICK_RATE_MS);

        // ESP_LOGI(TAG, "ticks %d", ticks_since_last_packet);

#ifdef TX
        /* Send some data to the broadcast address. */
        beastsquib_espnow_send_param_t *send_param = (beastsquib_espnow_send_param_t *)pvParameter;
        if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
            ESP_LOGE(TAG, "Send error");
            beastsquib_espnow_deinit(send_param);
            vTaskDelete(NULL);
        }

        ESP_LOGI(TAG, "sent data");
#endif
    }
}

static esp_err_t beastsquib_espnow_init(void)
{
    beastsquib_espnow_send_param_t *send_param;

    beastsquib_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(beastsquib_espnow_event_t));
    if (beastsquib_espnow_queue == NULL) {
        ESP_LOGE(TAG, "Create mutex fail");
        return ESP_FAIL;
    }

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK( esp_now_init() );
    ESP_ERROR_CHECK( esp_now_register_send_cb(beastsquib_espnow_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(beastsquib_espnow_recv_cb) );
    ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK) );

    /* Add broadcast peer information to peer list. */
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        ESP_LOGE(TAG, "Malloc peer information fail");
        vSemaphoreDelete(beastsquib_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }

    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, beastsquib_broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK( esp_now_add_peer(peer) );
    free(peer);

    /* Initialize sending parameters. */
    send_param = malloc(sizeof(beastsquib_espnow_send_param_t));
    memset(send_param, 0, sizeof(beastsquib_espnow_send_param_t));
    if (send_param == NULL) {
        ESP_LOGE(TAG, "Malloc send parameter fail");
        vSemaphoreDelete(beastsquib_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }

    send_param->unicast = false;
    send_param->broadcast = true;
    send_param->state = 0;
    send_param->magic = esp_random();
    send_param->count = CONFIG_ESPNOW_SEND_COUNT;
    send_param->delay = CONFIG_ESPNOW_SEND_DELAY;
    send_param->len = CONFIG_ESPNOW_SEND_LEN;
    send_param->buffer = malloc(CONFIG_ESPNOW_SEND_LEN);

    if (send_param->buffer == NULL) {
        ESP_LOGE(TAG, "Malloc send buffer fail");
        free(send_param);
        vSemaphoreDelete(beastsquib_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }

    memcpy(send_param->dest_mac, beastsquib_broadcast_mac, ESP_NOW_ETH_ALEN);
    beastsquib_espnow_data_prepare(send_param);

    xTaskCreate(beastsquib_espnow_task, "beastsquib_espnow_task", 2048, NULL, 4, NULL);
    xTaskCreate(tx_transmit_task, "tx_transmit_task", 2048, send_param, 4, NULL);

    return ESP_OK;
}

static void beastsquib_espnow_deinit(beastsquib_espnow_send_param_t *send_param)
{
    free(send_param->buffer);
    free(send_param);
    vSemaphoreDelete(beastsquib_espnow_queue);
    esp_now_deinit();
}

void hw_timer_callback(void *arg)
{
    ticks_since_last_packet ++;

    if (ticks_since_last_packet > 100)
    {
        gpio_set_level(GPIO_OUTPUT_LED, 1);
        gpio_set_level(GPIO_OUTPUT_BOOM, 0);
    }
}

#define EX_UART_NUM UART_NUM_0

#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)
static QueueHandle_t uart0_queue;

char uart_command_buffer[9];

static void read_board_id_cb(void)
{
    // Check if destination file exists before reading
    struct stat st;
    if (stat("/spiffs/boardid.txt", &st) == 0) {
        // Open renamed file for reading
        ESP_LOGI(TAG, "Reading file");
        FILE* f = fopen("/spiffs/boardid.txt", "r");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open file for reading");
            return;
        }
        char line[64];
        fgets(line, sizeof(line), f);
        fclose(f);
        // strip newline
        char* pos = strchr(line, '\n');
        if (pos) {
            *pos = '\0';
        }
        ESP_LOGI(TAG, "Read from file: '%s'", line);
    }
    else
    {
        ESP_LOGE(TAG, "No board_id file found");
    }
}

static void set_board_id_cb(void)
{
    // Parse the board id buffer
    char board_id[4];
    memset(board_id, 0, 4);
    memcpy(board_id, uart_command_buffer+5, 3);
    ESP_LOGI(TAG, "board_id: %s", board_id);

    struct stat st;
    if (stat("/spiffs/boardid.txt", &st) == 0) {
        // Delete it if it exists
        unlink("/spiffs/boardid.txt");
    }

    ESP_LOGI(TAG, "Opening file");
    FILE* f = fopen("/spiffs/boardid.txt", "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "%s\n", board_id);
    fclose(f);
    ESP_LOGI(TAG, "File written");
}

static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t *dtmp = (uint8_t *) malloc(RD_BUF_SIZE);

    for (;;) {
        // Waiting for UART event.
        if (xQueueReceive(uart0_queue, (void *)&event, (portTickType)portMAX_DELAY)) {
            bzero(dtmp, RD_BUF_SIZE);

            switch (event.type) {
                // Event of UART receving data
                // We'd better handler data event fast, there would be much more data events than
                // other types of events. If we take too much time on data event, the queue might be full.
                case UART_DATA:
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);

                    for (uint16_t i = 0; i < event.size; i ++)
                    {
                        memmove(uart_command_buffer, uart_command_buffer + 1, sizeof(uart_command_buffer) - 1);
                        uart_command_buffer[sizeof(uart_command_buffer) - 1] = dtmp[i];

                        if (memcmp(uart_command_buffer, "#SID,", 4) == 0 && uart_command_buffer[8] == ';')
                        {
                            set_board_id_cb();
                        }
                        
                        if (memcmp(uart_command_buffer, "#RID,", 4) == 0 && uart_command_buffer[5] == ';')
                        {
                            read_board_id_cb();
                        }

                    }
                    uart_write_bytes(EX_UART_NUM, (const char *) dtmp, event.size);
                    break;

                // Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    ESP_LOGI(TAG, "hw fifo overflow");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;

                // Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    ESP_LOGI(TAG, "ring buffer full");
                    // If buffer full happened, you should consider encreasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;

                case UART_PARITY_ERR:
                    ESP_LOGI(TAG, "uart parity error");
                    break;

                // Event of UART frame error
                case UART_FRAME_ERR:
                    ESP_LOGI(TAG, "uart frame error");
                    break;

                // Others
                default:
                    ESP_LOGI(TAG, "uart event type: %d", event.type);
                    break;
            }
        }
    }

    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

void app_main()
{
    // Initialize NVS
    ESP_ERROR_CHECK( nvs_flash_init() );

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = GPIO_OUTPUT_PIN_SEL,
        .pull_down_en = 0,
        .pull_up_en = 0
    };

    ESP_LOGI(TAG, "Initialize GPIO");
    gpio_set_level(GPIO_OUTPUT_LED, 1);
    gpio_set_level(GPIO_OUTPUT_BOOM, 0);
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "Initialize TIMER");
    hw_timer_init(hw_timer_callback, NULL);
    hw_timer_alarm_us(1000, true);

    // Configure parameters of an UART driver
    uart_config_t uart_config = {
        .baud_rate = 74880,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    
    // Install UART driver, and get the queue.
    ESP_LOGI(TAG, "Initialize UART");
    uart_param_config(EX_UART_NUM, &uart_config);
    uart_driver_install(EX_UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 100, &uart0_queue, 0);
    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 12, NULL);

    // Get board ID
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true
    };
    
    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    // Create the boardid file if it does not exist.
    struct stat st;
    if (stat("/spiffs/boardid.txt", &st) != 0) {
        ESP_LOGI(TAG, "Opening file");
        FILE* f = fopen("/spiffs/boardid.txt", "w");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open file for writing");
            return;
        }
        fprintf(f, "000\n");
        fclose(f);
        ESP_LOGI(TAG, "File written");
    }

    read_board_id_cb();

    beastsquib_wifi_init();
    beastsquib_espnow_init();
}
