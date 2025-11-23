/* ESP32 WPA/WPA2 Handshake Capture Tool - ILI9341 2.4" display (240x320)

   Features:
   - Passively capture WPA/WPA2 4-way handshakes
   - Display handshake capture progress (EAPOL frames 1-4)
   - Track multiple APs and client associations
   - Show security analysis and handshake strength
   - Educational tool for understanding WiFi security
   - Cyberpunk hacker-style UI with real-time stats

   Educational Purpose Only - Follow Local Laws!

   Wiring (Software SPI - SAME AS TETRIS):
     TFT VCC  -> 3.3V
     TFT GND  -> GND
     TFT MOSI -> GPIO 23
     TFT SCK  -> GPIO 18
     TFT CS   -> GPIO 5
     TFT DC   -> GPIO 21
     TFT RST  -> GPIO 4
     TFT LED  -> 3.3V
*/

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>

extern "C"
{
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_timer.h"
}

// ----- TFT pins (Software SPI) -----
#define TFT_CS 5
#define TFT_DC 21
#define TFT_RST 4
#define TFT_MOSI 23
#define TFT_SCLK 18

// Use software SPI constructor: (CS, DC, MOSI, SCLK, RST)
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// ----- Display Configuration -----
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define HEADER_HEIGHT 30
#define FOOTER_HEIGHT 18
#define CONTENT_START 32
#define LINE_HEIGHT 16

// ----- Colors -----
#define COLOR_BG 0x0000      // Black
#define COLOR_HEADER 0x001F  // Blue
#define COLOR_ACCENT 0x07FF  // Cyan
#define COLOR_WARNING 0xFFE0 // Yellow
#define COLOR_DANGER 0xF800  // Red
#define COLOR_SUCCESS 0x07E0 // Green
#define COLOR_TEXT 0xFFFF    // White
#define COLOR_DIM 0x8410     // Gray
#define COLOR_PURPLE 0xF81F  // Magenta

// ----- Handshake Configuration -----
#define MAX_APS 20
#define MAX_CLIENTS_PER_AP 10
#define EAPOL_FRAME_TYPE 0x888E
#define HANDSHAKE_TIMEOUT_MS 300000 // 5 minutes

// ----- Data Structures -----
struct ClientInfo
{
    uint8_t mac[6];
    int8_t rssi;
    uint32_t lastSeen;
    bool eapol1;
    bool eapol2;
    bool eapol3;
    bool eapol4;
    uint32_t handshakeTime; // Time when handshake completed
    uint8_t keyVersion;     // WPA1=1, WPA2=2, WPA3=3
    int8_t handshakeRSSI;   // Signal strength during capture
};

struct AccessPoint
{
    uint8_t bssid[6];
    char ssid[33];
    uint8_t channel;
    int8_t rssi;
    uint8_t encryption; // 0=open, 1=WEP, 2=WPA, 3=WPA2, 4=WPA3
    uint32_t lastSeen;
    uint32_t beaconCount;
    ClientInfo clients[MAX_CLIENTS_PER_AP];
    uint8_t clientCount;
    uint8_t completedHandshakes;
    bool hasHandshake;
};

AccessPoint aps[MAX_APS];
uint8_t apCount = 0;
uint32_t totalEapolFrames = 0;
uint32_t totalHandshakes = 0;
uint32_t totalBeacons = 0;
uint32_t totalDataFrames = 0;

// Display modes
enum DisplayMode
{
    MODE_CAPTURE,
    MODE_HANDSHAKES,
    MODE_ANALYSIS,
    MODE_STATS
};
DisplayMode currentMode = MODE_CAPTURE;
unsigned long lastModeSwitch = 0;
#define MODE_SWITCH_INTERVAL 8000

// Display refresh control
unsigned long lastDisplayUpdate = 0;
#define DISPLAY_UPDATE_INTERVAL 100 // Update display every 500ms
bool needsFullRedraw = true;
bool needsContentUpdate = false;

// Previous values for partial updates
uint8_t prevApCount = 0;
uint32_t prevTotalHandshakes = 0;
uint32_t prevTotalEapol = 0;
uint8_t prevChannel = 1;
DisplayMode prevMode = MODE_CAPTURE;

// Current channel
volatile uint8_t currentChannel = 1;

// ----- Helper Functions -----
bool macEqual(const uint8_t *a, const uint8_t *b)
{
    for (int i = 0; i < 6; i++)
        if (a[i] != b[i])
            return false;
    return true;
}

void macToString(const uint8_t *mac, char *str)
{
    sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void macToStringShort(const uint8_t *mac, char *str)
{
    sprintf(str, "%02X:%02X:%02X:%02X",
            mac[2], mac[3], mac[4], mac[5]);
}

const char *encryptionToString(uint8_t enc)
{
    switch (enc)
    {
    case 0:
        return "Open";
    case 1:
        return "WEP";
    case 2:
        return "WPA";
    case 3:
        return "WPA2";
    case 4:
        return "WPA3";
    default:
        return "Unknown";
    }
}

// ----- Detect encryption type from beacon -----
uint8_t detectEncryption(const uint8_t *payload, uint16_t len)
{
    // Look for RSN (WPA2) and WPA information elements
    bool hasRSN = false;
    bool hasWPA = false;

    for (uint16_t i = 36; i < len - 2; i++)
    {
        if (payload[i] == 48)
            hasRSN = true; // RSN IE
        if (payload[i] == 221 && i + 6 < len)
        {
            // Check for WPA OUI
            if (payload[i + 2] == 0x00 && payload[i + 3] == 0x50 &&
                payload[i + 4] == 0xF2 && payload[i + 5] == 0x01)
            {
                hasWPA = true;
            }
        }
    }

    if (hasRSN)
        return 3; // WPA2
    if (hasWPA)
        return 2; // WPA
    // Check for privacy bit (simplified WEP detection)
    if (len > 34 && (payload[34] & 0x10))
        return 1; // WEP
    return 0;     // Open
}

// ----- Find or add AP -----
int findAP(const uint8_t *bssid)
{
    for (int i = 0; i < apCount; i++)
    {
        if (macEqual(aps[i].bssid, bssid))
            return i;
    }
    return -1;
}

int addAP(const uint8_t *bssid, const char *ssid, uint8_t channel, int8_t rssi, uint8_t encryption)
{
    if (apCount >= MAX_APS)
        return -1;

    memcpy(aps[apCount].bssid, bssid, 6);
    strncpy(aps[apCount].ssid, ssid ? ssid : "<Hidden>", 32);
    aps[apCount].ssid[32] = 0;
    aps[apCount].channel = channel;
    aps[apCount].rssi = rssi;
    aps[apCount].encryption = encryption;
    aps[apCount].lastSeen = millis();
    aps[apCount].beaconCount = 1;
    aps[apCount].clientCount = 0;
    aps[apCount].completedHandshakes = 0;
    aps[apCount].hasHandshake = false;

    return apCount++;
}

// ----- Find or add client to AP -----
int findClient(AccessPoint *ap, const uint8_t *mac)
{
    for (int i = 0; i < ap->clientCount; i++)
    {
        if (macEqual(ap->clients[i].mac, mac))
            return i;
    }
    return -1;
}

int addClient(AccessPoint *ap, const uint8_t *mac, int8_t rssi)
{
    if (ap->clientCount >= MAX_CLIENTS_PER_AP)
        return -1;

    memcpy(ap->clients[ap->clientCount].mac, mac, 6);
    ap->clients[ap->clientCount].rssi = rssi;
    ap->clients[ap->clientCount].lastSeen = millis();
    ap->clients[ap->clientCount].eapol1 = false;
    ap->clients[ap->clientCount].eapol2 = false;
    ap->clients[ap->clientCount].eapol3 = false;
    ap->clients[ap->clientCount].eapol4 = false;
    ap->clients[ap->clientCount].handshakeTime = 0;
    ap->clients[ap->clientCount].keyVersion = 0;
    ap->clients[ap->clientCount].handshakeRSSI = rssi;

    return ap->clientCount++;
}

// ----- WiFi Sniffer Callback -----
IRAM_ATTR void wifi_sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    const uint8_t *payload = pkt->payload;
    uint16_t len = pkt->rx_ctrl.sig_len;

    if (len < 24)
        return; // Too short

    uint8_t frameType = payload[0];
    uint8_t frameSubtype = (payload[0] >> 4) & 0x0F;

    // Beacon frame (0x80)
    if (frameType == 0x80)
    {
        totalBeacons++;

        if (len < 38)
            return;

        uint8_t bssid[6];
        memcpy(bssid, payload + 16, 6);

        // Extract SSID
        char ssid[33] = {0};
        uint8_t ssidLen = payload[37];
        if (ssidLen > 0 && ssidLen <= 32 && len >= 38 + ssidLen)
        {
            memcpy(ssid, payload + 38, ssidLen);
            ssid[ssidLen] = 0;
        }

        uint8_t encryption = detectEncryption(payload, len);
        uint8_t channel = pkt->rx_ctrl.channel;
        int8_t rssi = pkt->rx_ctrl.rssi;

        int apIdx = findAP(bssid);
        if (apIdx < 0)
        {
            addAP(bssid, ssid, channel, rssi, encryption);
        }
        else
        {
            aps[apIdx].lastSeen = millis();
            aps[apIdx].beaconCount++;
            aps[apIdx].rssi = rssi;
        }
    }
    // Data frame (0x08)
    else if ((frameType & 0x0C) == 0x08)
    {
        totalDataFrames++;

        if (len < 32)
            return;

        // Check for EAPOL (802.1X authentication)
        // EAPOL frames have LLC header at offset 24 (for WPA)
        if (len >= 38)
        {
            // Check for SNAP header: AA AA 03 00 00 00 and EtherType 888E
            uint8_t llcOffset = 24;
            if (payload[llcOffset] == 0xAA &&
                payload[llcOffset + 1] == 0xAA &&
                payload[llcOffset + 2] == 0x03)
            {
                uint16_t etherType = (payload[llcOffset + 6] << 8) | payload[llcOffset + 7];
                if (etherType == EAPOL_FRAME_TYPE)
                {
                    totalEapolFrames++;

                    // Get MAC addresses
                    uint8_t receiverAddr[6], transmitterAddr[6], bssid[6];
                    memcpy(receiverAddr, payload + 4, 6);
                    memcpy(transmitterAddr, payload + 10, 6);
                    memcpy(bssid, payload + 16, 6);

                    // Find the AP
                    int apIdx = findAP(bssid);
                    if (apIdx < 0)
                    {
                        // Create AP entry if not found
                        apIdx = addAP(bssid, NULL, pkt->rx_ctrl.channel, pkt->rx_ctrl.rssi, 3);
                    }

                    if (apIdx >= 0)
                    {
                        // Determine client MAC (not the BSSID)
                        uint8_t clientMAC[6];
                        if (macEqual(transmitterAddr, bssid))
                        {
                            memcpy(clientMAC, receiverAddr, 6);
                        }
                        else
                        {
                            memcpy(clientMAC, transmitterAddr, 6);
                        }

                        // Find or add client
                        int clientIdx = findClient(&aps[apIdx], clientMAC);
                        if (clientIdx < 0)
                        {
                            clientIdx = addClient(&aps[apIdx], clientMAC, pkt->rx_ctrl.rssi);
                        }

                        if (clientIdx >= 0)
                        {
                            ClientInfo *client = &aps[apIdx].clients[clientIdx];
                            client->lastSeen = millis();
                            client->rssi = pkt->rx_ctrl.rssi;

                            // Parse EAPOL packet to determine message number
                            if (len >= 99)
                            {
                                uint8_t eapolOffset = llcOffset + 8;
                                uint8_t keyInfo1 = payload[eapolOffset + 5];
                                uint8_t keyInfo2 = payload[eapolOffset + 6];

                                // Key Info field analysis
                                bool pairwise = (keyInfo2 & 0x08) != 0;
                                bool install = (keyInfo2 & 0x40) != 0;
                                bool ack = (keyInfo2 & 0x80) != 0;
                                bool mic = (keyInfo1 & 0x01) != 0;

                                // Identify EAPOL message (1-4)
                                if (ack && !install && !mic)
                                {
                                    client->eapol1 = true;
                                    client->handshakeRSSI = pkt->rx_ctrl.rssi;
                                }
                                else if (!ack && !install && mic)
                                {
                                    client->eapol2 = true;
                                }
                                else if (ack && install && mic)
                                {
                                    client->eapol3 = true;
                                }
                                else if (!ack && !install && mic && pairwise)
                                {
                                    client->eapol4 = true;
                                }

                                // Check if we have a complete handshake
                                if (client->eapol1 && client->eapol2 &&
                                    client->eapol3 && client->eapol4 &&
                                    client->handshakeTime == 0)
                                {
                                    client->handshakeTime = millis();
                                    client->keyVersion = 2; // Assume WPA2
                                    aps[apIdx].completedHandshakes++;
                                    aps[apIdx].hasHandshake = true;
                                    totalHandshakes++;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// ----- Matrix Boot Animation -----
void matrixBootAnimation()
{
    const int numColumns = 40;
    int columnY[numColumns];
    int columnSpeed[numColumns];

    for (int i = 0; i < numColumns; i++)
    {
        columnY[i] = random(-SCREEN_HEIGHT, 0);
        columnSpeed[i] = random(4, 10);
    }

    tft.fillScreen(ILI9341_BLACK);
    tft.setTextSize(1);

    unsigned long startTime = millis();
    while (millis() - startTime < 2000)
    {
        for (int i = 0; i < numColumns; i++)
        {
            int x = (i * SCREEN_WIDTH) / numColumns;

            tft.setTextColor(ILI9341_WHITE);
            tft.setCursor(x, columnY[i]);
            tft.write(random(33, 126));

            for (int trail = 1; trail < 8; trail++)
            {
                int ty = columnY[i] - (trail * 8);
                if (ty >= 0 && ty < SCREEN_HEIGHT)
                {
                    uint16_t fadeColor = tft.color565(0, 255 - (trail * 30), 0);
                    tft.setTextColor(fadeColor);
                    tft.setCursor(x, ty);
                    tft.write(random(33, 126));
                }
            }

            columnY[i] += columnSpeed[i];
            if (columnY[i] > SCREEN_HEIGHT)
            {
                columnY[i] = random(-80, 0);
                columnSpeed[i] = random(4, 10);
            }
        }
        delay(30);
    }

    // Fade to black
    for (int fade = 0; fade < 5; fade++)
    {
        tft.fillScreen(ILI9341_BLACK);
        delay(80);
    }

    // Display logo
    tft.fillScreen(ILI9341_BLACK);
    tft.drawRect(10, 40, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 80, COLOR_DANGER);
    tft.drawRect(11, 41, SCREEN_WIDTH - 22, SCREEN_HEIGHT - 82, COLOR_DANGER);

    tft.setTextSize(3);
    tft.setTextColor(COLOR_DANGER);

    // Glitch effect
    for (int i = 0; i < 4; i++)
    {
        tft.setCursor(45, 70);
        tft.print("HANDSHAKE");
        delay(100);
        tft.fillRect(45, 70, 230, 24, ILI9341_BLACK);
        delay(50);
    }

    tft.setCursor(45, 70);
    tft.setTextColor(COLOR_DANGER);
    tft.print("HANDSHAKE");

    tft.setTextSize(2);
    tft.setTextColor(COLOR_PURPLE);
    tft.setCursor(220, 95);
    tft.print("HUNTER");

    tft.setTextSize(1);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(70, 105);
    tft.print("WPA/WPA2 Capture Tool");

    tft.setTextColor(COLOR_DIM);
    tft.setCursor(85, 120);
    tft.print("Educational Use Only");

    delay(800);

    // Boot sequence
    tft.setTextColor(COLOR_ACCENT);
    int bootY = 145;

    const char *bootMsgs[] = {
        "> Initializing ESP32...",
        "> WiFi promiscuous mode ON",
        "> EAPOL filter active",
        "> Handshake detector armed",
        "> Starting capture..."};

    for (int i = 0; i < 5; i++)
    {
        tft.setCursor(20, bootY);
        tft.print(bootMsgs[i]);
        bootY += 12;
        delay(200);
    }

    delay(600);
    tft.fillScreen(ILI9341_BLACK);
}

// ----- Draw Header -----
void drawHeader(bool forceRedraw = false)
{
    // Only redraw if values changed or forced
    bool modeChanged = (currentMode != prevMode);
    bool statsChanged = (apCount != prevApCount ||
                         totalHandshakes != prevTotalHandshakes ||
                         totalEapolFrames != prevTotalEapol ||
                         currentChannel != prevChannel);

    if (!forceRedraw && !modeChanged && !statsChanged)
    {
        return; // Nothing changed, skip redraw
    }

    // Full header redraw on mode change or force
    if (forceRedraw || modeChanged)
    {
        tft.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_BG);
        tft.drawFastHLine(0, HEADER_HEIGHT - 1, SCREEN_WIDTH, COLOR_HEADER);
        tft.drawFastHLine(0, HEADER_HEIGHT - 2, SCREEN_WIDTH, COLOR_HEADER);

        tft.setTextSize(2);
        tft.setCursor(5, 5);
        tft.setTextColor(COLOR_DANGER, COLOR_BG);

        switch (currentMode)
        {
        case MODE_CAPTURE:
            tft.print("CAPTURE   ");
            break;
        case MODE_HANDSHAKES:
            tft.print("HANDSHAKES");
            break;
        case MODE_ANALYSIS:
            tft.print("ANALYSIS  ");
            break;
        case MODE_STATS:
            tft.print("STATISTICS");
            break;
        }
        prevMode = currentMode;
    }

    // Update stats area (right side) - only if changed
    if (forceRedraw || statsChanged)
    {
        tft.setTextSize(1);

        // Clear stats area
        tft.fillRect(175, 5, SCREEN_WIDTH - 175, 22, COLOR_BG);

        tft.setCursor(180, 8);
        tft.setTextColor(COLOR_SUCCESS, COLOR_BG);
        tft.print("APs:");
        tft.setTextColor(COLOR_TEXT, COLOR_BG);
        tft.print(apCount);
        tft.print(" ");

        tft.setCursor(230, 8);
        tft.setTextColor(COLOR_WARNING, COLOR_BG);
        tft.print("HS:");
        tft.setTextColor(COLOR_TEXT, COLOR_BG);
        tft.print(totalHandshakes);
        tft.print(" ");

        tft.setCursor(180, 18);
        tft.setTextColor(COLOR_ACCENT, COLOR_BG);
        tft.print("CH:");
        tft.setTextColor(COLOR_TEXT, COLOR_BG);
        tft.print(currentChannel);
        tft.print("  ");

        tft.setCursor(230, 18);
        tft.setTextColor(COLOR_PURPLE, COLOR_BG);
        tft.print("EA:");
        tft.setTextColor(COLOR_TEXT, COLOR_BG);
        tft.print(totalEapolFrames);
        tft.print("  ");

        prevApCount = apCount;
        prevTotalHandshakes = totalHandshakes;
        prevTotalEapol = totalEapolFrames;
        prevChannel = currentChannel;
    }
}

// ----- Draw Footer -----
void drawFooter()
{
    int footerY = SCREEN_HEIGHT - FOOTER_HEIGHT;
    tft.fillRect(0, footerY, SCREEN_WIDTH, FOOTER_HEIGHT, COLOR_BG);
    tft.drawFastHLine(0, footerY, SCREEN_WIDTH, COLOR_HEADER);

    tft.setTextSize(1);
    tft.setCursor(5, footerY + 4);
    tft.setTextColor(COLOR_WARNING);
    tft.print("MODE:");
    tft.setTextColor(COLOR_TEXT);

    switch (currentMode)
    {
    case MODE_CAPTURE:
        tft.print("LIVE CAPTURE");
        break;
    case MODE_HANDSHAKES:
        tft.print("CAPTURED");
        break;
    case MODE_ANALYSIS:
        tft.print("ANALYSIS");
        break;
    case MODE_STATS:
        tft.print("STATS");
        break;
    }

    tft.setCursor(150, footerY + 4);
    tft.setTextColor(COLOR_SUCCESS);
    unsigned long runtime = millis() / 1000;
    tft.print("UP: ");
    tft.setTextColor(COLOR_TEXT);
    tft.print(runtime);
    tft.print("s");

    tft.setCursor(230, footerY + 4);
    tft.setTextColor(COLOR_ACCENT);
    tft.print("BCN:");
    tft.setTextColor(COLOR_TEXT);
    if (totalBeacons < 10000)
    {
        tft.print(totalBeacons);
    }
    else
    {
        tft.print(totalBeacons / 1000);
        tft.print("k");
    }
}

// ----- Draw Capture Mode -----
void drawCaptureMode()
{
    int y = CONTENT_START;
    int contentHeight = SCREEN_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT;
    tft.fillRect(0, CONTENT_START, SCREEN_WIDTH, contentHeight, COLOR_BG);

    if (apCount == 0)
    {
        tft.setCursor(70, SCREEN_HEIGHT / 2 - 10);
        tft.setTextColor(COLOR_WARNING);
        tft.setTextSize(2);
        tft.print("Scanning...");
        tft.setTextSize(1);
        tft.setCursor(60, SCREEN_HEIGHT / 2 + 10);
        tft.setTextColor(COLOR_DIM);
        tft.print("Waiting for WiFi traffic");
        return;
    }

    // Sort APs by handshake status then RSSI
    for (int i = 0; i < apCount - 1; i++)
    {
        for (int j = i + 1; j < apCount; j++)
        {
            bool swap = false;
            if (aps[j].hasHandshake && !aps[i].hasHandshake)
                swap = true;
            else if (aps[j].hasHandshake == aps[i].hasHandshake && aps[j].rssi > aps[i].rssi)
                swap = true;

            if (swap)
            {
                AccessPoint temp = aps[i];
                aps[i] = aps[j];
                aps[j] = temp;
            }
        }
    }

    tft.setTextSize(1);
    tft.setTextColor(COLOR_DIM);
    tft.setCursor(5, y);
    tft.print("SSID");
    tft.setCursor(110, y);
    tft.print("ENC");
    tft.setCursor(155, y);
    tft.print("CH");
    tft.setCursor(180, y);
    tft.print("RSSI");
    tft.setCursor(220, y);
    tft.print("CL");
    tft.setCursor(245, y);
    tft.print("HS");
    tft.setCursor(275, y);
    tft.print("ST");

    y += 10;
    tft.drawFastHLine(0, y, SCREEN_WIDTH, COLOR_DIM);
    y += 2;

    int maxDisplay = min(apCount, (uint8_t)11);
    for (int i = 0; i < maxDisplay; i++)
    {
        // SSID
        tft.setTextColor(aps[i].hasHandshake ? COLOR_SUCCESS : COLOR_TEXT);
        tft.setCursor(5, y);
        String ssid = String(aps[i].ssid);
        if (ssid.length() > 14)
            ssid = ssid.substring(0, 13) + "~";
        tft.print(ssid);

        // Encryption
        tft.setTextColor(aps[i].encryption >= 2 ? COLOR_WARNING : COLOR_DIM);
        tft.setCursor(110, y);
        const char *encStr = encryptionToString(aps[i].encryption);
        tft.print(encStr);

        // Channel
        tft.setTextColor(COLOR_ACCENT);
        tft.setCursor(155, y);
        tft.print(aps[i].channel);

        // RSSI
        uint16_t rssiColor = aps[i].rssi > -60 ? COLOR_SUCCESS : (aps[i].rssi > -80 ? COLOR_WARNING : COLOR_DANGER);
        tft.setTextColor(rssiColor);
        tft.setCursor(180, y);
        tft.print(aps[i].rssi);

        // Client count
        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(220, y);
        tft.print(aps[i].clientCount);

        // Handshake count
        if (aps[i].completedHandshakes > 0)
        {
            tft.setTextColor(COLOR_SUCCESS);
            tft.setCursor(245, y);
            tft.print(aps[i].completedHandshakes);
        }

        // Status indicator
        if (aps[i].hasHandshake)
        {
            tft.fillCircle(285, y + 4, 3, COLOR_SUCCESS);
        }
        else if (aps[i].clientCount > 0)
        {
            tft.fillCircle(285, y + 4, 3, COLOR_WARNING);
        }
        else
        {
            tft.drawCircle(285, y + 4, 3, COLOR_DIM);
        }

        y += LINE_HEIGHT;
    }
}

// ----- Draw Handshakes Mode -----
void drawHandshakesMode()
{
    int y = CONTENT_START;
    int contentHeight = SCREEN_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT;
    tft.fillRect(0, CONTENT_START, SCREEN_WIDTH, contentHeight, COLOR_BG);

    if (totalHandshakes == 0)
    {
        tft.setCursor(50, SCREEN_HEIGHT / 2 - 10);
        tft.setTextColor(COLOR_WARNING);
        tft.setTextSize(2);
        tft.print("No Handshakes");
        tft.setTextSize(1);
        tft.setCursor(55, SCREEN_HEIGHT / 2 + 10);
        tft.setTextColor(COLOR_DIM);
        tft.print("Waiting for EAPOL frames...");
        return;
    }

    tft.setTextSize(1);

    int displayed = 0;
    for (int i = 0; i < apCount && displayed < 6; i++)
    {
        if (aps[i].hasHandshake)
        {
            // AP header
            tft.setTextColor(COLOR_SUCCESS);
            tft.setCursor(5, y);
            tft.print(">");

            tft.setTextColor(COLOR_ACCENT);
            tft.setCursor(15, y);
            String ssid = String(aps[i].ssid);
            if (ssid.length() > 18)
                ssid = ssid.substring(0, 17) + "~";
            tft.print(ssid);

            tft.setTextColor(COLOR_WARNING);
            tft.setCursor(160, y);
            tft.print(encryptionToString(aps[i].encryption));

            tft.setTextColor(COLOR_TEXT);
            tft.setCursor(215, y);
            tft.print("CH:");
            tft.print(aps[i].channel);

            tft.setTextColor(COLOR_SUCCESS);
            tft.setCursor(260, y);
            tft.print("RSSI:");
            tft.print(aps[i].rssi);

            y += 12;

            // BSSID
            tft.setTextColor(COLOR_DIM);
            tft.setCursor(15, y);
            tft.print("BSSID: ");
            tft.setTextColor(COLOR_TEXT);
            char bssidStr[18];
            macToString(aps[i].bssid, bssidStr);
            tft.print(bssidStr);

            y += 12;

            // Show clients with complete handshakes
            for (int j = 0; j < aps[i].clientCount; j++)
            {
                ClientInfo *client = &aps[i].clients[j];
                if (client->eapol1 && client->eapol2 && client->eapol3 && client->eapol4)
                {
                    tft.setTextColor(COLOR_DIM);
                    tft.setCursor(20, y);
                    tft.print("Client: ");

                    tft.setTextColor(COLOR_SUCCESS);
                    char clientStr[18];
                    macToString(client->mac, clientStr);
                    tft.print(clientStr);

                    // Handshake progress bars
                    tft.setCursor(20, y + 10);
                    tft.setTextColor(COLOR_DIM);
                    tft.print("4-Way:");

                    int barX = 65;
                    int barY = y + 10;
                    int barW = 10;
                    int barH = 6;
                    int spacing = 12;

                    // EAPOL 1
                    if (client->eapol1)
                        tft.fillRect(barX, barY, barW, barH, COLOR_SUCCESS);
                    else
                        tft.drawRect(barX, barY, barW, barH, COLOR_DIM);

                    // EAPOL 2
                    if (client->eapol2)
                        tft.fillRect(barX + spacing, barY, barW, barH, COLOR_SUCCESS);
                    else
                        tft.drawRect(barX + spacing, barY, barW, barH, COLOR_DIM);

                    // EAPOL 3
                    if (client->eapol3)
                        tft.fillRect(barX + spacing * 2, barY, barW, barH, COLOR_SUCCESS);
                    else
                        tft.drawRect(barX + spacing * 2, barY, barW, barH, COLOR_DIM);

                    // EAPOL 4
                    if (client->eapol4)
                        tft.fillRect(barX + spacing * 3, barY, barW, barH, COLOR_SUCCESS);
                    else
                        tft.drawRect(barX + spacing * 3, barY, barW, barH, COLOR_DIM);

                    tft.setCursor(125, y + 10);
                    tft.setTextColor(COLOR_SUCCESS);
                    tft.print("COMPLETE");

                    y += 22;
                }
            }

            displayed++;
            y += 3;
        }
    }
}

// ----- Draw Analysis Mode -----
void drawAnalysisMode()
{
    int y = CONTENT_START;
    int contentHeight = SCREEN_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT;
    tft.fillRect(0, CONTENT_START, SCREEN_WIDTH, contentHeight, COLOR_BG);

    if (totalHandshakes == 0)
    {
        tft.setCursor(60, SCREEN_HEIGHT / 2 - 10);
        tft.setTextColor(COLOR_WARNING);
        tft.setTextSize(2);
        tft.print("No Analysis");
        tft.setTextSize(1);
        tft.setCursor(50, SCREEN_HEIGHT / 2 + 10);
        tft.setTextColor(COLOR_DIM);
        tft.print("Capture handshakes first");
        return;
    }

    tft.setTextSize(1);

    // Security Analysis Header
    tft.setTextColor(COLOR_WARNING);
    tft.setCursor(10, y);
    tft.print("SECURITY ANALYSIS:");
    y += 15;

    // Find AP with strongest handshake signal
    int strongestIdx = -1;
    int8_t strongestRSSI = -100;
    for (int i = 0; i < apCount; i++)
    {
        if (aps[i].hasHandshake)
        {
            for (int j = 0; j < aps[i].clientCount; j++)
            {
                if (aps[i].clients[j].handshakeTime > 0 &&
                    aps[i].clients[j].handshakeRSSI > strongestRSSI)
                {
                    strongestIdx = i;
                    strongestRSSI = aps[i].clients[j].handshakeRSSI;
                }
            }
        }
    }

    if (strongestIdx >= 0)
    {
        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(15, y);
        tft.print("Strongest Capture:");
        y += 12;

        tft.setTextColor(COLOR_ACCENT);
        tft.setCursor(20, y);
        String ssid = String(aps[strongestIdx].ssid);
        if (ssid.length() > 30)
            ssid = ssid.substring(0, 29) + "~";
        tft.print(ssid);
        y += 12;

        tft.setTextColor(COLOR_DIM);
        tft.setCursor(20, y);
        tft.print("Signal: ");
        uint16_t qualColor = strongestRSSI > -60 ? COLOR_SUCCESS : (strongestRSSI > -75 ? COLOR_WARNING : COLOR_DANGER);
        tft.setTextColor(qualColor);
        tft.print(strongestRSSI);
        tft.print(" dBm");

        tft.setTextColor(COLOR_DIM);
        tft.setCursor(150, y);
        tft.print("Quality: ");
        tft.setTextColor(qualColor);
        if (strongestRSSI > -60)
            tft.print("EXCELLENT");
        else if (strongestRSSI > -75)
            tft.print("GOOD");
        else
            tft.print("WEAK");

        y += 15;
    }

    y += 5;

    // Handshake Strength Distribution
    tft.setTextColor(COLOR_WARNING);
    tft.setCursor(10, y);
    tft.print("HANDSHAKE QUALITY:");
    y += 15;

    int excellent = 0, good = 0, weak = 0;
    for (int i = 0; i < apCount; i++)
    {
        if (aps[i].hasHandshake)
        {
            for (int j = 0; j < aps[i].clientCount; j++)
            {
                if (aps[i].clients[j].handshakeTime > 0)
                {
                    if (aps[i].clients[j].handshakeRSSI > -60)
                        excellent++;
                    else if (aps[i].clients[j].handshakeRSSI > -75)
                        good++;
                    else
                        weak++;
                }
            }
        }
    }

    if (excellent + good + weak > 0)
    {
        tft.setCursor(15, y);
        tft.setTextColor(COLOR_SUCCESS);
        tft.print("Excellent (>-60dBm):");
        tft.setTextColor(COLOR_TEXT);
        tft.print(" ");
        tft.print(excellent);
        int barLen = map(excellent, 0, totalHandshakes, 0, 100);
        tft.fillRect(200, y + 2, barLen, 6, COLOR_SUCCESS);
        y += 12;

        tft.setCursor(15, y);
        tft.setTextColor(COLOR_WARNING);
        tft.print("Good (>-75dBm):");
        tft.setTextColor(COLOR_TEXT);
        tft.print("     ");
        tft.print(good);
        barLen = map(good, 0, totalHandshakes, 0, 100);
        tft.fillRect(200, y + 2, barLen, 6, COLOR_WARNING);
        y += 12;

        tft.setCursor(15, y);
        tft.setTextColor(COLOR_DANGER);
        tft.print("Weak (<-75dBm):");
        tft.setTextColor(COLOR_TEXT);
        tft.print("      ");
        tft.print(weak);
        barLen = map(weak, 0, totalHandshakes, 0, 100);
        tft.fillRect(200, y + 2, barLen, 6, COLOR_DANGER);
        y += 15;
    }

    y += 5;

    // Encryption types
    tft.setTextColor(COLOR_WARNING);
    tft.setCursor(10, y);
    tft.print("ENCRYPTION TYPES:");
    y += 15;

    int wpa = 0, wpa2 = 0, wpa3 = 0;
    for (int i = 0; i < apCount; i++)
    {
        if (aps[i].hasHandshake)
        {
            if (aps[i].encryption == 2)
                wpa++;
            else if (aps[i].encryption == 3)
                wpa2++;
            else if (aps[i].encryption == 4)
                wpa3++;
        }
    }

    if (wpa > 0)
    {
        tft.setCursor(15, y);
        tft.setTextColor(COLOR_DANGER);
        tft.print("WPA (weak): ");
        tft.setTextColor(COLOR_TEXT);
        tft.print(wpa);
        y += 12;
    }

    if (wpa2 > 0)
    {
        tft.setCursor(15, y);
        tft.setTextColor(COLOR_WARNING);
        tft.print("WPA2: ");
        tft.setTextColor(COLOR_TEXT);
        tft.print(wpa2);
        y += 12;
    }

    if (wpa3 > 0)
    {
        tft.setCursor(15, y);
        tft.setTextColor(COLOR_SUCCESS);
        tft.print("WPA3 (strong): ");
        tft.setTextColor(COLOR_TEXT);
        tft.print(wpa3);
        y += 12;
    }

    y += 10;

    // Educational note
    tft.setTextColor(COLOR_DIM);
    tft.setCursor(10, y);
    tft.print("Note: Signal strength affects");
    y += 10;
    tft.setCursor(10, y);
    tft.print("handshake capture quality.");
}

// ----- Draw Stats Mode -----
void drawStatsMode()
{
    int y = CONTENT_START;
    int contentHeight = SCREEN_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT;
    tft.fillRect(0, CONTENT_START, SCREEN_WIDTH, contentHeight, COLOR_BG);

    tft.setTextSize(1);

    // Capture Statistics
    tft.setTextColor(COLOR_WARNING);
    tft.setCursor(10, y);
    tft.print("CAPTURE STATISTICS:");
    y += 15;

    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(15, y);
    tft.print("Total APs discovered:");
    tft.setTextColor(COLOR_SUCCESS);
    tft.setCursor(200, y);
    tft.print(apCount);
    y += 12;

    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(15, y);
    tft.print("Handshakes captured:");
    tft.setTextColor(COLOR_SUCCESS);
    tft.setCursor(200, y);
    tft.print(totalHandshakes);
    y += 12;

    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(15, y);
    tft.print("EAPOL frames seen:");
    tft.setTextColor(COLOR_PURPLE);
    tft.setCursor(200, y);
    tft.print(totalEapolFrames);
    y += 12;

    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(15, y);
    tft.print("Beacon frames:");
    tft.setTextColor(COLOR_ACCENT);
    tft.setCursor(200, y);
    if (totalBeacons < 10000)
        tft.print(totalBeacons);
    else
    {
        tft.print(totalBeacons / 1000);
        tft.print("k");
    }
    y += 12;

    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(15, y);
    tft.print("Data frames:");
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(200, y);
    if (totalDataFrames < 10000)
        tft.print(totalDataFrames);
    else
    {
        tft.print(totalDataFrames / 1000);
        tft.print("k");
    }
    y += 20;

    // Channel activity
    tft.setTextColor(COLOR_WARNING);
    tft.setCursor(10, y);
    tft.print("CHANNEL ACTIVITY:");
    y += 15;

    uint16_t channelCounts[14] = {0};
    for (int i = 0; i < apCount; i++)
    {
        if (aps[i].channel >= 1 && aps[i].channel <= 13)
        {
            channelCounts[aps[i].channel]++;
        }
    }

    int maxCount = 0;
    for (int i = 1; i <= 13; i++)
    {
        if (channelCounts[i] > maxCount)
            maxCount = channelCounts[i];
    }

    // Draw top 5 busiest channels
    int topChannels[5] = {0};
    int topCounts[5] = {0};
    for (int i = 1; i <= 13; i++)
    {
        for (int j = 0; j < 5; j++)
        {
            if (channelCounts[i] > topCounts[j])
            {
                for (int k = 4; k > j; k--)
                {
                    topChannels[k] = topChannels[k - 1];
                    topCounts[k] = topCounts[k - 1];
                }
                topChannels[j] = i;
                topCounts[j] = channelCounts[i];
                break;
            }
        }
    }

    for (int i = 0; i < 5 && topCounts[i] > 0; i++)
    {
        tft.setCursor(15, y);
        tft.setTextColor(COLOR_ACCENT);
        char buf[20];
        sprintf(buf, "Channel %2d:", topChannels[i]);
        tft.print(buf);

        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(100, y);
        tft.print(topCounts[i]);
        tft.print(" APs");

        if (maxCount > 0)
        {
            int barLen = map(topCounts[i], 0, maxCount, 0, 100);
            tft.fillRect(160, y + 2, barLen, 6, COLOR_ACCENT);
        }

        y += 12;
    }

    y += 10;

    // Runtime stats
    unsigned long runtime = millis() / 1000;
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(15, y);
    tft.print("Runtime: ");
    tft.setTextColor(COLOR_SUCCESS);
    tft.print(runtime / 60);
    tft.print("m ");
    tft.print(runtime % 60);
    tft.print("s");
}

// ----- Channel Hopping -----
void setChannel(uint8_t ch)
{
    if (ch < 1)
        ch = 1;
    if (ch > 13)
        ch = 13;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    currentChannel = ch;
}

// ----- Setup -----
void setup()
{
    // Initialize TFT
    tft.begin();
    tft.setRotation(0); // Landscape: 320x240
    tft.setTextWrap(false);

    // Boot animation
    matrixBootAnimation();

    // Initialize WiFi in promiscuous mode
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_NULL);
    esp_wifi_start();

    // Set promiscuous filter for management and data frames
    wifi_promiscuous_filter_t filter;
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filter);

    esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_cb);
    esp_wifi_set_promiscuous(true);

    setChannel(1);

    // Initial UI - force full redraw
    needsFullRedraw = true;

    // Draw initial screen
    drawHeader(true);
    drawCaptureMode();
    drawFooter();
}

// ----- Main Loop -----
uint8_t hopChannel = 1;
unsigned long lastHop = 0;
const unsigned long HOP_MS = 250; // Channel hop every 250ms

void loop()
{
    unsigned long now = millis();

    // Channel hopping
    if (now - lastHop >= HOP_MS)
    {
        hopChannel++;
        if (hopChannel > 13)
            hopChannel = 1;
        setChannel(hopChannel);
        lastHop = now;
    }

    // Auto-switch display modes
    if (now - lastModeSwitch >= MODE_SWITCH_INTERVAL)
    {
        currentMode = (DisplayMode)((currentMode + 1) % 4);
        lastModeSwitch = now;
        needsFullRedraw = true;
    }

    // Check if data has changed (to trigger content update)
    bool dataChanged = (apCount != prevApCount ||
                        totalHandshakes != prevTotalHandshakes ||
                        totalEapolFrames != prevTotalEapol ||
                        currentChannel != prevChannel);

    // Update display intelligently
    if (needsFullRedraw)
    {
        // Full screen redraw (mode change or startup)
        drawHeader(true);

        switch (currentMode)
        {
        case MODE_CAPTURE:
            drawCaptureMode();
            break;
        case MODE_HANDSHAKES:
            drawHandshakesMode();
            break;
        case MODE_ANALYSIS:
            drawAnalysisMode();
            break;
        case MODE_STATS:
            drawStatsMode();
            break;
        }

        drawFooter();
        needsFullRedraw = false;
        lastDisplayUpdate = now;
    }
    else if (dataChanged || (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL))
    {
        // Partial update - only header stats and content that changed
        drawHeader(false); // Smart header update

        // Update content area
        switch (currentMode)
        {
        case MODE_CAPTURE:
            drawCaptureMode();
            break;
        case MODE_HANDSHAKES:
            drawHandshakesMode();
            break;
        case MODE_ANALYSIS:
            drawAnalysisMode();
            break;
        case MODE_STATS:
            drawStatsMode();
            break;
        }

        drawFooter();
        lastDisplayUpdate = now;
    }

    delay(25);
}
