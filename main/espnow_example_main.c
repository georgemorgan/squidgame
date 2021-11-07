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
#include <stdint.h>
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

#define BEASTSQUIB_MAGIC_NUMBER 0xB3A57

static const char *TAG = "beast_squib";
static xQueueHandle beastsquib_espnow_queue;
static uint8_t beastsquib_broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// #define TX
#define RX

#if defined(TX) && defined(RX)
#error Cannot define both TX and RX
#endif

int board_id = -1;
uint16_t test_board_id = 0;
beastsquib_espnow_data_t global_tx_data;

#ifdef RX
// RX
#define GPIO_OUTPUT_PYRO 15
#define GPIO_OUTPUT_PYRO_MASK (1ULL << GPIO_OUTPUT_PYRO)
#define GPIO_OUTPUT_ARMED_LED 16
#define GPIO_OUTPUT_ARMED_MASK (1ULL << GPIO_OUTPUT_ARMED_LED)

/* Kill command variables. */
bool pyro_armed = false;

#define LOW 0
#define HIGH 1

/* Enables the PYRO GPIO pin (sets to OUTPUT with pull down) */
static inline void SET_ARMED() {
    // Set LED to LOW (on)
    gpio_set_level(GPIO_OUTPUT_ARMED_LED, LOW);

    pyro_armed = true;
}

/* Disables the PYRO GPIO pin (sets to tri-state with pull down) */
static inline void SET_DISARMED() {
    // Set LED to HIGH (off)
    gpio_set_level(GPIO_OUTPUT_ARMED_LED, HIGH);
    
    pyro_armed = false;
}

/* Detonates the PYRO! */
static inline void DETONATE() {
    if (pyro_armed) {
        // Set PYRO GPIO to HIGH (detonate)
        gpio_set_level(GPIO_OUTPUT_PYRO, HIGH);
    }
}

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
    esp_wifi_set_max_tx_power(84);
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
int beastsquib_validate_espnow_data_checksum(uint8_t *data, uint16_t data_len)
{
    beastsquib_espnow_data_t *buf = (beastsquib_espnow_data_t *)data;
    uint16_t crc, crc_cal = 0;

    if (data_len < sizeof(beastsquib_espnow_data_t)) {
        ESP_LOGE(TAG, "Receive ESPNOW data too short, len:%d", data_len);
        return -1;
    }

    crc = buf->crc;
    buf->crc = 0;
    crc_cal = crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len);

    // Validate CRC and MAGIC
    if ((crc_cal == crc) && (buf->magic == BEASTSQUIB_MAGIC_NUMBER)) {
        return 0;
    }

    // Error parsing packet
    return -1;
}

/* Prepare ESPNOW data to be sent. */
void beastsquib_espnow_data_prepare(beastsquib_espnow_send_param_t *send_param)
{
    beastsquib_espnow_data_t *send_buffer = (beastsquib_espnow_data_t *)send_param->buffer;
    assert(send_param->len >= sizeof(beastsquib_espnow_data_t));
    send_buffer->crc = 0;
    send_buffer->magic = send_param->magic;
    send_buffer->crc = crc16_le(UINT16_MAX, (uint8_t const *)send_buffer, send_param->len);
}

static bool get_bit(uint8_t *bits_list)
{
    if (board_id == -1) {
        return false;
    }

    // Modulo 64 gets the index into the armed list
    uint16_t idx = board_id / 8;
    uint8_t offset = board_id % 8;
    uint8_t bits = bits_list[idx];
    bool set = ((bits & (1 << offset)) != 0);
    
    // ESP_LOGI(TAG, "idx '%i', offset '%i', bit '%s'", (int)idx, (int)offset, (set) ? "T" : "F");

    return set;
}

static void print_bytes(uint8_t *bits_list) {
    for (int i = 0; i < 64; i ++)
    {
        ESP_LOGI(TAG, "byte 0x%02x", *(uint8_t *)(bits_list + i));
    }
}

/* Called when a broadcast packet is received. */
static void espnow_broadcast_packet_recv_cb(beastsquib_espnow_data_t *data) {
#ifdef RX
    // ESP_LOGI(TAG, "idx '%d'", data[0]);

    // ESP_LOGI(TAG, "armed_bits: ");
    // print_bytes(data->armed_bits);
    // ESP_LOGI(TAG, "pyro_bits: ");
    // print_bytes(data->pyro_bits);

    // Gets armed bit associated with this board ID
    if (data->armed == 1)
    {
        ESP_LOGI(TAG, "ARMED");
        SET_ARMED();
    }
    else
    {
        SET_DISARMED();
    }

    if (get_bit(data->pyro_bits))
    {
        ESP_LOGI(TAG, "DETONATE");
        DETONATE();
    }
#endif
}

static void beastsquib_espnow_task(void *pvParameter)
{
    beastsquib_espnow_event_t evt;

    while (xQueueReceive(beastsquib_espnow_queue, &evt, portMAX_DELAY) == pdTRUE) {
        switch (evt.id) {
            case BEASTSQUIB_ESPNOW_SEND_CB:
            {
                /* SENT DATA */
                break;
            }
            case BEASTSQUIB_ESPNOW_RECV_CB:
            {
                /* RECEIVED DATA */
                ticks_since_last_packet = 0;

                beastsquib_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
                if (beastsquib_validate_espnow_data_checksum(recv_cb->data, recv_cb->data_len) == 0)
                {
                    espnow_broadcast_packet_recv_cb(recv_cb->data);
                }

                free(recv_cb->data);

                break;
            }
            default:
                ESP_LOGE(TAG, "Callback type error: %d", evt.id);
                break;
        }
    }
}

static void beastsquib_espnow_deinit(beastsquib_espnow_send_param_t *send_param)
{
    free(send_param->buffer);
    free(send_param);
    vSemaphoreDelete(beastsquib_espnow_queue);
    esp_now_deinit();
}

#ifdef TX

static void tx_transmit_task(void *pvParameter)
{
    while (1)
    {
        vTaskDelay(100 / portTICK_RATE_MS);

        // ESP_LOGI(TAG, "ticks %d", ticks_since_last_packet);

        beastsquib_espnow_send_param_t *send_param = (beastsquib_espnow_send_param_t *)pvParameter;
        beastsquib_espnow_data_t *data = send_param->buffer;

        memcpy(data, &global_tx_data, sizeof(global_tx_data));

        /* Arm or disarm all the boards */
        // data->armed = global_armed_state;

        /* Trigger the test_board_id */
        // uint8_t test_board_idx = test_board_id / 8;
        // uint8_t test_board_offset = test_board_id % 8;
        // data->pyro_bits[test_board_idx] = (1 << test_board_offset);

        // ESP_LOGI(TAG, "armed_bits: ");
        // print_bytes(data->armed_bits);
        // ESP_LOGI(TAG, "pyro_bits: ");
        // print_bytes(data->pyro_bits);

        // ESP_LOGI(TAG, "idx '%i', offset '%i'", (int)test_board_idx, (int)test_board_offset);

        beastsquib_espnow_data_prepare(send_param);

        /* Send some data to the broadcast address. */
        if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
            ESP_LOGE(TAG, "Send error");
            beastsquib_espnow_deinit(send_param);
            vTaskDelete(NULL);
        }

        // ESP_LOGI(TAG, "sent data");
    }
}

#endif

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
    if (send_param == NULL) {
        ESP_LOGE(TAG, "Malloc send parameter fail");
        vSemaphoreDelete(beastsquib_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(send_param, 0, sizeof(beastsquib_espnow_send_param_t));

    // Configure dest mac, magic number, send length, and buffer
    memcpy(send_param->dest_mac, beastsquib_broadcast_mac, ESP_NOW_ETH_ALEN);
    send_param->magic = BEASTSQUIB_MAGIC_NUMBER;
    send_param->len = 200;
    send_param->buffer = malloc(200);

    if (send_param->buffer == NULL) {
        ESP_LOGE(TAG, "Malloc send buffer fail");
        free(send_param);
        vSemaphoreDelete(beastsquib_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }

    xTaskCreate(beastsquib_espnow_task, "beastsquib_espnow_task", 2048, NULL, 4, NULL);
  
#ifdef TX
    xTaskCreate(tx_transmit_task, "tx_transmit_task", 2048, send_param, 4, NULL);
#endif

    return ESP_OK;
}

#define EX_UART_NUM UART_NUM_0
#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)
static QueueHandle_t uart0_queue;
char uart_command_buffer[128+6];

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

        board_id = atoi(line);

        ESP_LOGI(TAG, "board_id: '%i'", board_id);
    }
    else
    {
        ESP_LOGE(TAG, "No board_id file found");
    }
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

                        void *end_buffer = uart_command_buffer + sizeof(uart_command_buffer) - 1;

                        // #SID,000;
                        if (memcmp(end_buffer-8, "#SID,", 4) == 0 && *(uint8_t *)end_buffer == ';')
                        {
                            // Parse the board id buffer
                            char board_id[4];
                            memset(board_id, 0, 4);
                            memcpy(board_id, end_buffer-3, 3);
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
                        
                        // #RID,;
                        if (memcmp(end_buffer-5, "#RID,", 4) == 0 && *(uint8_t *)end_buffer == ';')
                        {
                            read_board_id_cb();
                        }

                        // #TID,000;
                        if (memcmp(end_buffer-8, "#TID,", 4) == 0 && *(uint8_t *)end_buffer == ';')
                        {
                            char board_id[4];
                            memset(board_id, 0, 4);
                            memcpy(board_id, end_buffer-3, 3);
                            test_board_id = atoi(board_id);
                            ESP_LOGI(TAG, "test_board_id: %i", test_board_id);
                        }

                        // #ARM,0;
                        if (memcmp(end_buffer-6, "#ARM,", 4) == 0 && *(uint8_t *)end_buffer == ';')
                        {
                            // Parse the board id buffer
                            char armed_bit[1];
                            memset(armed_bit, 0, 1);
                            memcpy(armed_bit, end_buffer-1, 1);
                            global_tx_data.armed = atoi(armed_bit);
                            ESP_LOGI(TAG, "global_armed_state: %i", global_tx_data.armed);
                        }

                        if (memcmp(uart_command_buffer, "#DET,", 4) == 0 && *(uint8_t *)end_buffer == ';')
                        {
                            void *start_hex = uart_command_buffer + 5;
                            char byte[3];
                            memset(byte, 0, 3);

                            for (int i = 0; i < 64; i ++)
                            {
                                memcpy(byte, start_hex + 2*i, 2);
                                global_tx_data.pyro_bits[i] = strtol(byte, NULL, 16);
                            }

                            ESP_LOGI(TAG, "updated pyro data");
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

#define ESPNOW_SILENCE_TICKS_TIMEOUT 1000

/* Hardware timer keeps track of the number of ticks since the last received espnow packet
   If more than 100 ticks have elapsed, disarm the board.
*/
void hw_timer_callback(void *arg)
{
    ticks_since_last_packet ++;

    if (ticks_since_last_packet > ESPNOW_SILENCE_TICKS_TIMEOUT)
    {
        #ifdef RX
            SET_DISARMED();
        #endif
    }
}

void app_main()
{
    // Initialize NVS
    ESP_ERROR_CHECK( nvs_flash_init() );

    /* Only the receiver enables GPIO pins and the SPI filesystem. */
#ifdef RX

    gpio_config_t gpio_armed_pin_config = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = GPIO_OUTPUT_ARMED_MASK,
        .pull_down_en = 0,
        .pull_up_en = 1
    };

    ESP_LOGI(TAG, "Initialize GPIO");
    gpio_config(&gpio_armed_pin_config);

    gpio_config_t gpio_pyro_pin_config = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = GPIO_OUTPUT_PYRO_MASK,
        .pull_down_en = 1,
        .pull_up_en = 0
    };

    gpio_config(&gpio_pyro_pin_config);

    ESP_LOGI(TAG, "Initialize TIMER");
    hw_timer_init(hw_timer_callback, NULL);
    hw_timer_alarm_us(1000, true);

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

#endif

    // Configure parameters of an UART driver
    uart_config_t uart_config = {
        .baud_rate = 115200,
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

    memset(&global_tx_data, 0, sizeof(global_tx_data));

    beastsquib_wifi_init();
    beastsquib_espnow_init();
}
