/*
stolen from: https://github.com/spacehuhn/PacketMonitor32/blob/master/PacketMonitor32.ino
*/
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_event_loop.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <string>
#include <cstddef>
#include <Wire.h>
#include <Preferences.h>
#include <freefonts.h>
#include <Button2.h>

using namespace std;
#define MAX_CH 14       // 1 - 14
#define SNAP_LEN 2324   // max len of each recieved packet

// TTGO T-Display Buttons
#define BUTTON_UP  35
#define BUTTON_DOWN  0

#define USE_DISPLAY     // comment out if you don't want to use OLED
#define MAX_X 240
#define MAX_Y 135


#include <TFT_eSPI.h> // Hardware-specific library
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI();;       // Invoke custom library
TFT_eSprite rssi_graph = TFT_eSprite(&tft);
int grid = 0;
int tcount = 0;

esp_err_t event_handler(void* ctx, system_event_t* event) {
    Serial.printf("ERROR\n");
    return ESP_OK;
}

Preferences preferences;
uint32_t lastDrawTime;

Button2 button_up;
Button2 button_down;


uint32_t tmpPacketCounter;
uint32_t pkts[MAX_X];   // here the packets per second will be saved
uint32_t deauths = 0;   // deauth frames per second
unsigned int ch = 1;    // current 802.11 channel
int rssiSum;
int rssi = 0;

double getMultiplicator() {
    uint32_t maxVal = 1;
    for (int i = 0; i < MAX_X; i++) {
        if (pkts[i] > maxVal) maxVal = pkts[i];
    }
    if (maxVal > MAX_Y) return (double)MAX_Y / (double)maxVal;
    else return 1;
}

void setChannel(int newChannel) {
    ch = newChannel;
    Serial.printf("setting channel to %i\n", ch);
    if (ch > MAX_CH || ch < 1) ch = 1;
    preferences.begin("packetmonitor32", false);
    preferences.putUInt("channel", ch);
    preferences.end();
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous);
    esp_wifi_set_promiscuous(true);
}

void wifi_promiscuous(void* buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    wifi_pkt_rx_ctrl_t ctrl = (wifi_pkt_rx_ctrl_t)pkt->rx_ctrl;
    if (type == WIFI_PKT_MGMT && 
        (pkt->payload[0] == 0xA0 || pkt->payload[0] == 0xC0 )) 
        deauths++;
    if (type == WIFI_PKT_MISC) return;         // wrong packet type
    if (ctrl.sig_len > SNAP_LEN) return;       // packet too long
    uint32_t packetLength = ctrl.sig_len;
    if (type == WIFI_PKT_MGMT) 
        packetLength -= 4;  // fix for known bug in the IDF https://github.com/espressif/esp-idf/issues/886
    tmpPacketCounter++;
    rssiSum += ctrl.rssi;
}

void calculate_rssi() {
    // This is still a mistery, I stole this from someone.
    // double multiplicator = getMultiplicator();
    if (pkts[MAX_X - 1] > 0) rssi = rssiSum / (int)pkts[MAX_X - 1];
    else rssi = rssiSum;
}

void printAt(String s, int x, int y) {
    tft.drawString(s, x, y, GFXFF);
    Serial.println(s);
}

void setup() {
    // Serial
    Serial.begin(115200);

    // Init display
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    
    // Create a sprite for the graph
    rssi_graph.setColorDepth(8);
    rssi_graph.createSprite(MAX_X, MAX_Y);

    // Settings
    preferences.begin("packetmonitor32", false);
    ch = preferences.getUInt("channel", 1);
    preferences.end();
    // System & WiFi
    nvs_flash_init();
    tcpip_adapter_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    // ESP_ERROR_CHECK(esp_wifi_set_country(WIFI_COUNTRY_EU));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    // second core
    xTaskCreatePinnedToCore(
        core_task1,               /* Function to implement the task */
        "core_task1",             /* Name of the task */
        2500,                   /* Stack size in words */
        NULL,                   /* Task input parameter */
        0,                      /* Priority of the task */
        NULL,                   /* Task handle. */
        1);                     /* Core where the task should run */

    // start Wifi sniffer
    esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous);
    esp_wifi_set_promiscuous(true);
    
    // Init buttons
    init_buttons();
}


void draw_scroll_graph() {
    // Draw point in graph1 sprite at far right edge (this will scroll left later)
    rssi_graph.drawFastVLine(239, MAX_Y+rssi, 2, TFT_YELLOW); // draw 2 pixel point on graph

    // Push the sprites onto the TFT at specied coordinates
    rssi_graph.pushSprite(0, 0);

    rssi_graph.scroll(-1, 0); // scroll graph 1 pixel left, 0 up/down

    // Draw the grid on far right edge of sprite as graph has now moved 1 pixel left
    grid++;
    if (grid >= 20)
    { // Draw a vertical line if we have scrolled 10 times (10 pixels)
        grid = 0;
        rssi_graph.drawFastVLine(MAX_X-1, 0, MAX_Y, TFT_NAVY); // draw line on graph
    }
    
    else
    { // Otherwise draw points spaced 10 pixels for the horizontal grid lines
        for (int p = 0; p <= MAX_Y; p += 20) rssi_graph.drawPixel(MAX_X-1, p, TFT_NAVY);
    }
}

void draw_legend() {
    rssi_graph.fillRect(0, 0, 65, 22, TFT_BLACK);

    rssi_graph.setTextSize(1);
    rssi_graph.setFreeFont(FMB9);
    rssi_graph.setTextColor(TFT_GREEN);  // Black text, no background colour
    rssi_graph.setTextWrap(false);       // Turn of wrap so we can print past end of sprite

    // Need to print twice so text appears to wrap around at left and right edges
    rssi_graph.setCursor(2, 20);  // Print text at xpos
    rssi_graph.printf("CH: %i", ch);
}

void init_buttons() {
    button_up.begin(BUTTON_UP);
    button_down.begin(BUTTON_DOWN);
    button_up.setPressedHandler(button_handler);
    button_down.setPressedHandler(button_handler);
    button_down.setLongClickTime(2000);
    button_down.setLongClickDetectedHandler(power_down);
}

void power_down(Button2& btn) {
    esp_deep_sleep_start();
}

void button_handler(Button2& btn) {
    Serial.printf("BUTTON PRESS\n");
    switch (btn.getAttachPin()) {
        case BUTTON_UP:
            Serial.printf("BUTTON_UP SINGLE_CLICK\n");
            ch = (ch + 1) % 14;
            setChannel(ch);
            break;
        case BUTTON_DOWN:
            Serial.printf("BUTTON_DOWN SINGLE_CLICK\n");
            ch = (ch - 1) % 14;
            setChannel(ch);
            break;
    }
}


void loop() {
    delay(50);
    draw_scroll_graph();
    draw_legend();
}


void core_task1( void * p ) {
    /*
    This task handles button presses,
    serial output and channel changes
    */


    uint32_t currentTime;

    while(true) {
        button_up.loop();
        button_down.loop();
        currentTime = millis();
        if ( currentTime - lastDrawTime > 100 ) {
            lastDrawTime = currentTime;
            pkts[MAX_X - 1] = tmpPacketCounter;
            calculate_rssi();
            Serial.printf("rssi: %i\n", rssi);
            tmpPacketCounter = 0;
            deauths = 0;
            rssiSum = 0;
        }
        // Serial input
        if (Serial.available()) {
            ch = Serial.readString().toInt();
            if (ch < 1 || ch > 14) ch = 1;
            setChannel(ch);
        }
  }
}