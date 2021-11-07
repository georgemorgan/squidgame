/* ESPNOW Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#ifndef BEASTSQUIB_H
#define BEASTSQUIB_H

/* ESPNOW can work in both station and softap mode. It is configured in menuconfig. */
#if CONFIG_STATION_MODE
#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_STA
#else
#define ESPNOW_WIFI_MODE WIFI_MODE_AP
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_AP
#endif

#define ESPNOW_QUEUE_SIZE           6

#define IS_BROADCAST_ADDR(addr) (memcmp(addr, beastsquib_broadcast_mac, ESP_NOW_ETH_ALEN) == 0)

typedef enum {
    BEASTSQUIB_ESPNOW_SEND_CB,
    BEASTSQUIB_ESPNOW_RECV_CB,
} beastsquib_espnow_event_id_t;

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    esp_now_send_status_t status;
} beastsquib_espnow_event_send_cb_t;

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    uint8_t *data;
    int data_len;
} beastsquib_espnow_event_recv_cb_t;

typedef union {
    beastsquib_espnow_event_send_cb_t send_cb;
    beastsquib_espnow_event_recv_cb_t recv_cb;
} beastsquib_espnow_event_info_t;

/* When ESPNOW sending or receiving callback function is called, post event to ESPNOW task. */
typedef struct {
    beastsquib_espnow_event_id_t id;
    beastsquib_espnow_event_info_t info;
} beastsquib_espnow_event_t;

enum {
    beastsquib_ESPNOW_DATA_BROADCAST,
    beastsquib_ESPNOW_DATA_UNICAST,
    beastsquib_ESPNOW_DATA_MAX,
};

/* User defined field of ESPNOW data in this example. */
typedef struct {
    uint16_t crc;                         //CRC16 value of ESPNOW data.
    uint32_t magic;                       //Magic number which is used to determine which device to send unicast ESPNOW data.
    uint16_t padding; // Padding because crash
    uint64_t payload[16];                   //Real payload of ESPNOW data.
} __attribute__((packed)) beastsquib_espnow_data_t;

/* Parameters of sending ESPNOW data. */
typedef struct {
    uint32_t magic;                       //Magic number which is used to determine which device to send unicast ESPNOW data.
    int len;                              //Length of ESPNOW data to be sent, unit: byte.
    uint8_t *buffer;                      //Buffer pointing to ESPNOW data.
    uint8_t dest_mac[ESP_NOW_ETH_ALEN];   //MAC address of destination device.
} beastsquib_espnow_send_param_t;

#endif
