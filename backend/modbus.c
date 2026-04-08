#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "driver/uart.h"
#include "esp_intr_alloc.h"
#include "soc/uart_reg.h"
#include "soc/uart_struct.h"

#define WIFI_SSID "Redmi Note 10 Pro Max"
#define WIFI_PASSWORD "prince7175"

#define MQTT_BROKER "public-mqtt-broker.bevywise.com"
#define MQTT_PORT 1883
#define MQTT_CLIENT "ESP32_Modbus"

#define TOPIC_TX "modbus/debug/tx"
#define TOPIC_RX "modbus/debug/rx"
#define TOPIC_CMD "modbus/command/read"

#define RXD2 16
#define TXD2 17
#define DE_PIN 4

#define RX_BUFFER_SIZE 256
#define TX_BUFFER_SIZE 256

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Dynamic parameters
volatile uint8_t mb_slave_id = 1;
volatile uint16_t mb_start = 0;
volatile uint16_t mb_qty = 5;
volatile uint8_t mb_func = 0x03;

volatile uint16_t tick = 0;
volatile bool tx_flag = 0;
// volatile bool
// Buffers
uint8_t rx_buffer[RX_BUFFER_SIZE];
uint16_t rx_index = 0;
uint8_t tx_buffer[TX_BUFFER_SIZE];

// Flags
volatile bool frame_ready = false;
volatile bool tx_busy = false;
volatile uint16_t tx_head = 0;
volatile uint16_t tx_tail = 0;

hw_timer_t *timer = NULL;
void IRAM_ATTR ontimer() {

  tick++;
  if (tick >= 500) {
    tick = 0;
    tx_flag = true;
  }
}
// rs485  conteroller
void rs485_tx_enable() {
  digitalWrite(DE_PIN, HIGH);
}
void rs485_rx_enable() {
  digitalWrite(DE_PIN, LOW);
}

uint16_t modbus_crc16(uint8_t *buf, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (int pos = 0; pos < len; pos++) {
    crc ^= buf[pos];
    for (int i = 0; i < 8; i++)
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
  }
  return crc;
}
// btes to hex
void bytes_to_hex_string(uint8_t *data, uint16_t len, char *out, uint16_t out_size) {
  uint16_t index = 0;
  for (uint16_t i = 0; i < len; i++) {
    if (index >= out_size - 3) break;
    index += snprintf(out + index, out_size - index, "%02X ", data[i]);
  }
  out[index] = '\0';
}

void send_modbus_frame(uint8_t *frame, uint16_t len) {
  rs485_tx_enable();

  //   UART2.int_clr.tx_done = 1;
  //   // UART2.int_ena.tx_done = 1;
  //   while (UART2.status.txfifo_cnt);
  //   for (uint16_t i = 0; i < len; i++)
  //   {
  //     tx_buffer[tx_head] =frame[i];
  //     tx_head = (tx_head + 1) % TX_BUFFER_SIZE;
  //   }
  //   tx_busy = true;

  //   while ((tx_tail != tx_head) && (UART2.status.txfifo_cnt < 126))
  //   {
  //     UART2.fifo.rw_byte = tx_buffer[tx_tail++];
  //     //
  //     if (tx_tail >= TX_BUFFER_SIZE) tx_tail = 0;

  //   }
  // UART2.int_ena.tx_done=1;
  //   UART2.int_ena.tx_done = 1;
  //   if (tx_tail != tx_head)
  //     UART2.int_ena.txfifo_empty = 1;
  //\\\\\\\\\\\\\\\ methods//////////////////////////////////////////////////
  uart_write_bytes(UART_NUM_2, (const char *)frame, len);
  uart_wait_tx_done(UART_NUM_2, 100);

  rs485_rx_enable();
}

void modbus_master_read() {
  uint8_t frame[8];

  frame[0] = mb_slave_id;
  frame[1] = mb_func;
  frame[2] = mb_start >> 8;
  frame[3] = mb_start & 0xFF;
  frame[4] = mb_qty >> 8;
  frame[5] = mb_qty & 0xFF;

  uint16_t crc = modbus_crc16(frame, 6);
  frame[6] = crc & 0xFF;
  frame[7] = crc >> 8;

  // MQTT TX publish
  if (mqttClient.connected()) {
    char payload[128];
    char hex[50];

    bytes_to_hex_string(frame, 8, hex, sizeof(hex));

    // snprintf(payload, sizeof(payload), "%s", hex);
    StaticJsonDocument<128>doc;
    doc["type"]="tx";
    doc["slave"]=mb_slave_id;
    doc["start"]=mb_start;
    doc["qty"]=mb_qty;
    char json_out[128];
    serializeJson(doc,json_out);


    mqttClient.publish(TOPIC_TX, json_out);
  }
  tx_busy = true;
  send_modbus_frame(frame, 8);
  tx_busy = false;
}
void process_rx() {
  if (rx_index < 5) return;

  uint8_t byte_count = rx_buffer[2];
  uint16_t expected = 5 + byte_count;

  if (rx_index < expected) return;

  uint16_t crc_rx = rx_buffer[expected - 2] | (rx_buffer[expected - 1] << 8);
  uint16_t crc_calc = modbus_crc16(rx_buffer, expected - 2);

  if (crc_rx != crc_calc) {
    rx_index = 0;
    return;
  }
  if (mqttClient.connected()) {
    char payload[300];
    char hex[200];

    // HEX string
   
    // Base info
    // int offset = snprintf(payload, sizeof(payload), "%s", hex);

StaticJsonDocument<256>doc;
doc["type"]="rx";
doc["slave"]=rx_buffer[0];
 bytes_to_hex_string(rx_buffer, expected, hex, sizeof(hex));
    doc["reg"]=hex;
// JsonArray reg=doc.createNestedArray("reg");
char json_out[256];
serializeJson(doc,json_out);

    mqttClient.publish(TOPIC_RX,json_out);
  }
  rx_index = 0;
}
void mqtt_callback(char *topic, byte *payload, unsigned int length) {
  char cmd[50];
  memcpy(cmd, payload, length);
  cmd[length] = '\0';
  // sscanf(cmd, "%hhu,%hu,%hu,%hhu", &mb_slave_id, &mb_start, &mb_qty, &mb_func);
  StaticJsonDocument<128>doc;
DeserializationError err=deserializeJson(doc,payload,length);
if(err) return;
mb_slave_id=doc["slave"];
mb_start=doc["start"];
mb_qty=doc["qty"];

  modbus_master_read();
}
void wifi_connect() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
  }
}

void mqtt_reconnect() {
  while (!mqttClient.connected()) {
    if (mqttClient.connect(MQTT_CLIENT)) {
      mqttClient.subscribe(TOPIC_CMD);
    } else {
    }
  }
}

void setup() {
  Serial.begin(115200);

  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &ontimer);
  timerAlarm(timer, 10000, true, 0);

  pinMode(DE_PIN, OUTPUT);
  rs485_rx_enable();

  uart_config_t config = {
    .baud_rate = 9600,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
  };

  uart_param_config(UART_NUM_2, &config);
  uart_set_pin(UART_NUM_2, TXD2, RXD2, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_driver_install(UART_NUM_2, RX_BUFFER_SIZE, 0, 0, NULL, 0);
  wifi_connect();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqtt_callback);
}
void loop() {
  if (!mqttClient.connected())
    mqtt_reconnect();

  mqttClient.loop();

  // Read UART
  int len = uart_read_bytes(UART_NUM_2, rx_buffer + rx_index, RX_BUFFER_SIZE - rx_index, 20 / portTICK_PERIOD_MS);

  if (len > 0) {
    rx_index += len;
    frame_ready = true;
  }

  if (frame_ready) {
    frame_ready = false;
    process_rx();
  }
  if (!tx_busy && tx_flag) {
    tx_flag = false;
    modbus_master_read();
  }
}
