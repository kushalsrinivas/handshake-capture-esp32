# WPA/WPA2 Handshake Capture Tool

## Overview
A passive WiFi handshake capture tool for ESP32 with ILI9341 display. This educational tool monitors WiFi traffic to capture WPA/WPA2 4-way handshakes and provides real-time security analysis.

## ⚠️ Legal Disclaimer
**FOR EDUCATIONAL PURPOSES ONLY**

This tool is designed for learning about WiFi security, ethical security research, and authorized penetration testing. Unauthorized monitoring or interception of WiFi communications may violate laws in your jurisdiction. Always:
- Obtain proper authorization before testing
- Only use on networks you own or have explicit permission to test
- Follow all local, state, and federal laws
- Use responsibly and ethically

## Features

### 🎯 Core Functionality
1. **Passive Handshake Capture**
   - Monitors all WiFi channels (1-13)
   - Detects EAPOL (802.1X) authentication frames
   - Tracks 4-way handshake progress (frames 1-4)
   - Captures multiple handshakes simultaneously

2. **Access Point Tracking**
   - Discovers WiFi access points
   - Identifies encryption type (Open/WEP/WPA/WPA2/WPA3)
   - Tracks signal strength (RSSI)
   - Monitors client associations

3. **Security Analysis**
   - Handshake quality assessment
   - Signal strength analysis
   - Encryption type distribution
   - Capture success rate statistics

4. **Educational Display Modes**
   - **CAPTURE**: Live monitoring of APs and handshake progress
   - **HANDSHAKES**: Detailed view of captured handshakes
   - **ANALYSIS**: Security analysis and handshake quality
   - **STATISTICS**: Channel activity and capture stats

### 📊 Display Information

#### Capture Mode
- SSID of detected networks
- Encryption type (Open/WEP/WPA/WPA2/WPA3)
- Channel number
- Signal strength (RSSI)
- Connected clients count
- Handshake status indicators
- Visual status: ● (captured), ● (clients present), ○ (no activity)

#### Handshakes Mode
- Complete handshake details
- BSSID (MAC address of AP)
- Client MAC addresses
- 4-way handshake progress bars
- EAPOL frame capture status (1-4)
- Encryption type and channel

#### Analysis Mode
- **Strongest Capture**: Best quality handshake captured
- **Quality Distribution**: 
  - Excellent (>-60 dBm)
  - Good (>-75 dBm)
  - Weak (<-75 dBm)
- **Encryption Types**: WPA/WPA2/WPA3 distribution
- Educational notes on capture quality

#### Statistics Mode
- Total APs discovered
- Handshakes captured
- EAPOL frames detected
- Beacon frames received
- Data frames processed
- Channel activity (busiest channels)
- Runtime statistics

## Hardware Requirements

### Components
- **ESP32 Development Board** (ESP32-WROOM-32 or similar)
- **ILI9341 2.4" TFT Display** (320x240 resolution)
- Jumper wires
- Breadboard (optional)
- USB cable for power and programming

### Wiring (Software SPI - SAME AS TETRIS)

```
ILI9341 Display → ESP32
---------------------------------
VCC    → 3.3V
GND    → GND
MOSI   → GPIO 23
SCK    → GPIO 18
CS     → GPIO 5
DC     → GPIO 21
RST    → GPIO 4
LED    → 3.3V (backlight)
```

## Software Requirements

### Arduino IDE Setup
1. **Install ESP32 Board Support**
   - Open Arduino IDE
   - Go to File → Preferences
   - Add to "Additional Board Manager URLs":
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Go to Tools → Board → Board Manager
   - Search "ESP32" and install "esp32 by Espressif Systems"

2. **Install Required Libraries**
   - Adafruit GFX Library (by Adafruit)
   - Adafruit ILI9341 (by Adafruit)
   
   Install via: Sketch → Include Library → Manage Libraries

### Board Configuration
- **Board**: ESP32 Dev Module (or your specific ESP32 board)
- **Upload Speed**: 921600
- **Flash Frequency**: 80MHz
- **Flash Mode**: QIO
- **Flash Size**: 4MB (or match your board)
- **Partition Scheme**: Default 4MB with spiffs
- **Core Debug Level**: None (or "Info" for debugging)
- **Port**: Select your ESP32 COM port

## Installation

1. Open `handshake_capture_ili9341.ino` in Arduino IDE
2. Verify hardware connections match the wiring diagram
3. Select correct board and port
4. Click Upload
5. Watch the display for all status and capture information

## Usage

### Starting the Tool
1. Power on the ESP32
2. Watch the Matrix-style boot animation
3. The tool automatically starts scanning all channels
4. Handshake capture begins immediately

### Understanding the Display

#### Header Information
- **Mode Title**: Current display mode
- **APs**: Number of access points discovered
- **HS**: Total handshakes captured
- **CH**: Current channel being monitored
- **EA**: EAPOL frames detected

#### Status Indicators
- **Green Circle ●**: Handshake captured
- **Yellow Circle ●**: Clients present, waiting for handshake
- **Gray Circle ○**: No activity

#### Color Coding
- **Green**: Success, strong signal, captured
- **Yellow**: Warning, medium signal, in progress
- **Red**: Danger, weak signal, vulnerable
- **Cyan**: Information, highlight
- **Magenta**: Special status
- **Gray**: Inactive or background info

### Display Modes
The tool automatically cycles through 4 modes every 8 seconds:

1. **CAPTURE Mode**: Real-time AP monitoring
2. **HANDSHAKES Mode**: Captured handshakes
3. **ANALYSIS Mode**: Security analysis
4. **STATS Mode**: Statistics and metrics

### What to Look For

#### Capturing Handshakes
Handshakes occur when:
- A client connects to an AP
- A client reconnects after deauthentication
- An AP sends periodic reauthentication

**Best Practices**:
- Monitor channels with high activity
- Wait for client association events
- Strong signal (>-60 dBm) = better quality
- All 4 EAPOL frames needed for complete handshake

#### EAPOL Frame Sequence
1. **Message 1**: AP → Client (ANonce)
2. **Message 2**: Client → AP (SNonce + MIC)
3. **Message 3**: AP → Client (GTK + MIC)
4. **Message 4**: Client → AP (ACK + MIC)

## Technical Details

### How It Works

1. **Promiscuous Mode**: ESP32 WiFi is set to promiscuous mode to capture all packets
2. **Frame Filtering**: Monitors management (beacons) and data frames (EAPOL)
3. **Channel Hopping**: Cycles through channels 1-13 every 250ms
4. **EAPOL Detection**: Identifies 802.1X frames (EtherType 0x888E)
5. **Handshake Parsing**: Analyzes Key Info field to identify message 1-4
6. **Progress Tracking**: Maintains state for each client-AP pair

### Key Info Field Analysis
The tool examines the EAPOL Key Information field to determine message type:
- **Message 1**: ACK bit set, no MIC, no Install
- **Message 2**: MIC bit set, no ACK, no Install
- **Message 3**: ACK + Install + MIC bits set
- **Message 4**: MIC bit set, Pairwise, no ACK

### Memory Management
- Max 20 APs tracked simultaneously
- Max 10 clients per AP
- Automatic timeout for stale entries
- Efficient MAC address comparison

## Understanding Security

### WPA/WPA2 4-Way Handshake
The 4-way handshake is how WPA/WPA2 authenticates clients:
1. Proves both sides know the Pre-Shared Key (PSK)
2. Generates fresh encryption keys for the session
3. Uses Message Integrity Codes (MIC) to prevent tampering

### Why Capture Matters
A complete handshake contains:
- Authentication nonces (ANonce, SNonce)
- Message Integrity Codes (MICs)
- All data needed for offline password verification

### Signal Strength Impact
- **Excellent (>-60 dBm)**: Clear capture, all frames received
- **Good (>-75 dBm)**: Reliable, minor frame loss possible
- **Weak (<-75 dBm)**: May miss frames, incomplete handshakes

### Encryption Types
- **WPA**: TKIP encryption (deprecated, weaker)
- **WPA2**: AES-CCMP encryption (standard)
- **WPA3**: SAE authentication (strongest, harder to capture)

## Troubleshooting

### No APs Detected
- Check antenna connection
- Move to area with WiFi activity
- Verify WiFi is enabled on ESP32
- Display should show "Scanning..." message

### No Handshakes Captured
- Handshakes occur during authentication events
- Wait for devices to connect/reconnect
- Move closer to target AP (better signal)
- Monitor during high-activity periods
- Some devices use WPA3 (harder to capture)

### Display Issues
- Verify wiring connections
- Check TFT_CS, TFT_DC, TFT_RST pins
- Ensure 3.3V power is stable
- Try different rotation setting

### Memory Errors
- Reduce MAX_APS if running out of memory
- Reduce MAX_CLIENTS_PER_AP
- Device may reboot if out of memory

### Upload Errors
- Check USB cable and connection
- Try lower upload speed (115200)
- Press BOOT button during upload
- Verify correct board selection

## Advanced Modifications

### Change Channel Hop Speed
```cpp
const unsigned long HOP_MS = 250; // milliseconds
```
- Lower = faster scanning, less time per channel
- Higher = slower scanning, more time to capture

### Adjust Display Mode Timing
```cpp
#define MODE_SWITCH_INTERVAL 8000 // milliseconds
```

### Focus on Specific Channel
Comment out channel hopping in loop():
```cpp
// if (now - lastHop >= HOP_MS) {
//     hopChannel++;
//     ...
// }
setChannel(6); // Stay on channel 6
```

### Add Sound/LED Alerts
Add code when handshake is captured:
```cpp
if (client->eapol1 && client->eapol2 &&
    client->eapol3 && client->eapol4 &&
    client->handshakeTime == 0) {
    // Add your alert here
    // digitalWrite(LED_PIN, HIGH);
    // tone(BUZZER_PIN, 1000, 200);
}
```

## Performance Notes

### CPU Usage
- Channel hopping: minimal overhead
- Packet processing: occurs in WiFi callback (IRAM)
- Display updates: ~100ms interval
- Overall: Efficient, no lag

### Capture Rate
Depends on:
- WiFi activity in area
- Number of clients connecting
- Channel congestion
- Signal strength
- Proximity to APs

### Memory Usage
- ~8KB for AP tracking
- ~3KB for display buffer
- ~2KB for frame processing
- Total: ~15-20KB RAM

## Educational Applications

### Learn About
1. **WiFi Security Protocols**: WPA/WPA2/WPA3 mechanisms
2. **802.11 Frame Structure**: Management and data frames
3. **Authentication Process**: 4-way handshake sequence
4. **RF Propagation**: Signal strength and range
5. **Channel Planning**: Congestion and interference

### Security Research
- Test your own network security
- Evaluate AP placement and coverage
- Assess client roaming behavior
- Identify rogue access points
- Monitor for deauth attacks

### Classroom/Workshop Use
- Network security demonstrations
- WiFi protocol education
- Ethical hacking training
- Cybersecurity awareness
- Hands-on penetration testing labs

## Safety and Ethics

### Responsible Use
✅ **DO**:
- Use on your own networks
- Get written permission for testing
- Follow responsible disclosure
- Use for educational purposes
- Respect privacy and laws

❌ **DON'T**:
- Capture traffic without authorization
- Attempt to crack passwords for unauthorized access
- Interfere with others' communications
- Use for malicious purposes
- Violate computer fraud laws

### Legal Considerations
- **USA**: Computer Fraud and Abuse Act (CFAA), Wiretap Act
- **EU**: GDPR, Computer Misuse Act
- **Other regions**: Consult local laws

## Frequently Asked Questions

**Q: Can this crack WiFi passwords?**
A: No, this tool only captures handshakes. Password cracking requires additional offline analysis tools and is only legal on your own networks.

**Q: Why do I see EAPOL frames but no complete handshake?**
A: You need all 4 messages. Weak signal, client roaming, or timing can cause missed frames.

**Q: Does this work on WPA3?**
A: WPA3 uses SAE (Simultaneous Authentication of Equals), not the traditional 4-way handshake, making it much harder to capture.

**Q: How close do I need to be to the AP?**
A: Closer is better. Aim for RSSI >-60 dBm for reliable captures.

**Q: Can I save captured handshakes?**
A: The current version stores them in RAM. You could modify it to save to SD card or serial output.

**Q: Is this better than Aircrack-ng?**
A: This is a portable, standalone tool for education. Aircrack-ng is more comprehensive for professional use.

## Future Enhancements

Potential additions:
- [ ] SD card storage for handshakes
- [ ] PCAP file format export
- [ ] Deauthentication detection
- [ ] WPA3 SAE monitoring
- [ ] GPS coordinates for wardriving
- [ ] Web interface for remote monitoring
- [ ] Bluetooth controls for mode switching
- [ ] Audio alerts for captures

## Resources

### Learn More
- [WiFi 4-Way Handshake Explanation](https://en.wikipedia.org/wiki/IEEE_802.11i-2004)
- [ESP32 WiFi Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html)
- [EAPOL Protocol Specification](https://www.ietf.org/rfc/rfc3748.txt)

### Tools
- Wireshark: Packet analysis
- Aircrack-ng: WiFi security testing
- Hashcat: Password recovery

## Credits

Created for educational purposes to understand WiFi security mechanisms.

**Libraries Used**:
- Adafruit GFX Library
- Adafruit ILI9341
- ESP32 Arduino Core

## License

This code is provided for educational purposes. Use responsibly and legally.

---

**Remember**: With great power comes great responsibility. Use this knowledge to improve security, not to compromise it. Always operate within legal and ethical boundaries.

