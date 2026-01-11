/**
 * XYE Protocol Header for Midea Air Handlers
 * 
 * This file defines the XYE serial protocol used by Midea air handlers.
 * The protocol uses 16-byte command packets and 32-byte response packets.
 * 
 * Protocol Variants:
 *   There appear to be at least 2 variants of the XYE protocol:
 *   
 *   VARIANT A (Flachzange/HA Community - this implementation):
 *     - Mode at byte 11 (0x0B) in command packet
 *     - Used by: 410A air handlers, some mini-splits
 *   
 *   VARIANT B (mdrobnak/Codeberg XYE):
 *     - Mode at byte 6 (0x06) in command packet
 *     - Mode flags at byte 12 (0x0C)
 *     - Used by: Some RS485 units, water-based systems
 *     - See: github.com/mdrobnak/esphome midea_xye component
 *
 * Communication: 4800 baud, 8N1
 * 
 * Command Packet Structure (16 bytes, Variant A):
 *   [0x00]  0xAA - Start byte (preamble)
 *   [0x01]  0xC3 - Command type (0xC0=query, 0xC3=set, 0xCC=lock, 0xCD=unlock)
 *   [0x02]  0x00 - Server ID
 *   [0x03]  0x00 - Client ID
 *   [0x04]  0x80 - Unit ID / direction marker
 *   [0x05]  0x00 - Client ID
 *   [0x06]  0x00 - (Variant B has mode here)
 *   [0x07]  Fan mode
 *   [0x08]  Temperature setpoint
 *   [0x09]  Timer value 1 (0x00 if unused)
 *   [0x0A]  Timer value 2 (0x00 if unused)
 *   [0x0B]  Mode byte (Variant A - per Flachzange fix)
 *   [0x0C]  0x00 - (Variant B has mode flags here: ECO, AUX_HEAT, SWING)
 *   [0x0D]  0x3C (ctrl) or 0x3F (query) - inverted command byte
 *   [0x0E]  CRC (0xFF - sum of other bytes)
 *   [0x0F]  0x55 - End byte (prologue)
 * 
 * Response Packet Structure (32 bytes):
 *   [0]  0xAA - Start byte
 *   [1]  0xC0 - Response type (echo of command)
 *   [2]  0x00/0x80 - Direction/destination
 *   [3-5] Destination/source bytes
 *   [6]  Unknown
 *   [7]  Capabilities flags (0x80=ext_temp, 0x10=swing)
 *   [8]  Mode byte
 *   [9]  Fan byte
 *   [10] Temperature setpoint
 *   [11] T1 - Inlet air temperature
 *   [12] T2A - Coil A temperature
 *   [13] T2B - Coil B temperature
 *   [14] T3 - Outside/exhaust temperature
 *   [15] Current measurement (often 255/invalid)
 *   [16] Unknown
 *   [17] Timer start value
 *   [18] Timer stop value
 *   [19] Unknown
 *   [20] Mode flags (0x01=ECO, 0x02=AUX_HEAT, 0x04=SWING, 0x88=VENT)
 *   [21] Operation flags (0x04=WATER_PUMP, 0x80=WATER_LOCK)
 *   [22] Error flags (low byte)
 *   [23] Error flags (high byte)
 *   [24] Protection flags (low byte)
 *   [25] Protection flags (high byte)
 *   [26] CCM communication error flags
 *   [27-29] Reserved/Unknown
 *   [30] CRC checksum
 *   [31] 0x55 - End byte
 * 
 * Mode Bytes:
 *   0x00 - Off
 *   0x80 - Auto (some units)
 *   0x81 - Fan Only
 *   0x82 - Dry
 *   0x84 - Heat
 *   0x88 - Cool
 *   0x91 - Auto (other units)
 * 
 * Fan Bytes:
 *   0x80 - Auto
 *   0x01 - High
 *   0x02 - Medium
 *   0x04 - Low (NOT 0x03!)
 * 
 * References:
 *   - HA Community: community.home-assistant.io/t/midea-a-c-via-local-xye/857679
 *   - mdrobnak component: github.com/mdrobnak/esphome (midea_xye)
 *   - Codeberg XYE: codeberg.org/xye/xye
 *   - ESP32_Midea_RS485: github.com/Bunicutz/ESP32_Midea_RS485
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <string>

// Arduino/ESP32 includes
#include <Arduino.h>
#include <HardwareSerial.h>

// ============================================================================
// Hardware Configuration
// ============================================================================

// ESP32 UART2 pins (adjust if using different pins)
#define RX_PIN 16
#define TX_PIN 17

// ============================================================================
// Protocol Constants - Command Packet Indices
// ============================================================================

#define SEND_FAN    7   // Fan mode byte position
#define SEND_TEMP   8   // Temperature setpoint position
#define SEND_TIMER1 9   // Timer value 1 position
#define SEND_TIMER2 10  // Timer value 2 position
#define SEND_MODE   11  // Operating mode byte position (0x0B per Flachzange fix)
#define SEND_CRC    14  // CRC byte position (0x0E)
#define SEND_LEN    16  // Total command length

// ============================================================================
// Protocol Constants - Response Packet Indices
// ============================================================================

#define REC_MODE        8   // Operating mode in response
#define REC_FAN         9   // Fan mode in response
#define REC_TEMP        10  // Temperature setpoint in response
#define T1_INDEX        11  // Inlet air temperature (indoor)
#define T2A_INDEX       12  // Coil A temperature
#define T2B_INDEX       13  // Coil B temperature
#define T3_INDEX        14  // Outside/exhaust temperature
#define CURRENT_INDEX   15  // Current measurement (often 255=invalid)
#define TIMER_START_IDX 17  // Timer start value
#define TIMER_STOP_IDX  18  // Timer stop value
#define MODE_FLAGS_IDX  20  // Mode flags (ECO, AUX_HEAT, SWING, VENT)
#define OP_FLAGS_IDX    21  // Operation flags (WATER_PUMP, WATER_LOCK)
#define ERR1_INDEX      22  // Error flags (low byte) - some units use 23
#define ERR2_INDEX      23  // Error flags (high byte)
#define PROT1_INDEX     24  // Protection flags (low byte)
#define PROT2_INDEX     25  // Protection flags (high byte)
#define CCM_ERR_INDEX   26  // CCM communication error flags

// ============================================================================
// Mode Flags (Byte 20 in response, Byte 12 in command for Variant B)
// ============================================================================

#define MODE_FLAG_NORM     0x00  // Normal operation
#define MODE_FLAG_ECO      0x01  // ECO/Sleep mode
#define MODE_FLAG_AUX_HEAT 0x02  // Aux/Boost heating
#define MODE_FLAG_SWING    0x04  // Swing enabled
#define MODE_FLAG_VENT     0x88  // Ventilation mode

// ============================================================================
// Operation Flags (Byte 21 in response)
// ============================================================================

#define OP_FLAG_WATER_PUMP 0x04  // Water pump active
#define OP_FLAG_WATER_LOCK 0x80  // Water lock active

// ============================================================================
// Capability Flags (Byte 7 in response)
// ============================================================================

#define CAP_EXT_TEMP  0x80  // External temperature sensor supported
#define CAP_SWING     0x10  // Swing mode supported

// ============================================================================
// Known Error Codes (from community research)
// ============================================================================
// Error codes are unit-specific, but these are commonly seen:
// E0/0:    No error
// E1:      Indoor/outdoor communication error
// E2:      Indoor temperature sensor fault
// E3:      Indoor coil temperature sensor fault  
// E4:      Outdoor temperature sensor fault
// E5:      Outdoor coil temperature sensor fault
// E6:      Compressor overload
// E7:      Compressor overcurrent
// E8:      System high pressure
// E9:      System low pressure
// EA/10:   Compressor phase error
// EB/11:   Outdoor fan motor error
// EC/12:   Indoor fan motor error
// ED/13:   EEPROM error
// EE/14:   Power voltage error
// EF/15:   Freeze protection activated
// F0-F9:   Additional fault codes (varies by unit)

// ============================================================================
// Mode Constants
// ============================================================================

#define MODE_OFF        0x00
#define MODE_AUTO       0x91  // Auto mode (some units)
#define MODE_AUTO_ALT   0x80  // Auto mode (other units use 0x80)
#define MODE_COOL       0x88
#define MODE_DRY        0x82
#define MODE_HEAT       0x84
#define MODE_FAN_ONLY   0x81

// ============================================================================
// Fan Constants (per community research)
// ============================================================================

#define FAN_AUTO       0x80  // Automatic fan control
#define FAN_HIGH       0x01  // Maximum speed
#define FAN_MEDIUM     0x02  // Medium speed
#define FAN_MEDIUM_LOW 0x03  // Medium-Low (some units only)
#define FAN_LOW        0x04  // Low speed (NOT 0x03 per Flachzange fix)

// ============================================================================
// Serial Interface
// ============================================================================

#if defined(ESP8266)
    #define IS_8266 1
    HardwareSerial xyeSerial(0);
#else
    #define IS_8266 0
    HardwareSerial xyeSerial(2);  // UART2 on ESP32
#endif

// ============================================================================
// XYE Protocol State
// ============================================================================

class XYEState {
public:
    // Current state
    uint8_t setTemp = 72;           // Temperature setpoint (°F)
    uint8_t fanBytes = FAN_AUTO;    // Current fan mode
    uint8_t opBytes = MODE_OFF;     // Current operating mode
    
    // Communication state
    bool newInput = false;          // New command pending
    bool doneReading = false;       // Finished reading response
    bool waitingToSend = false;     // Command queued for sending
    bool waitingForResponse = false; // Waiting for response
    bool commandSent = false;       // Command was just sent
    
    // Timing/counters
    uint8_t waitCount = 0;          // Response wait cycles
    uint8_t prevResp = 0;           // Previous buffer size (for stale data detection)
    uint8_t sendTimeCount = 0;      // Input debounce counter
    
    // Data buffers
    uint8_t recData[30] = {0};      // Received data buffer
    
    // Command packet template
    // Mode is at byte 11 (0x0B), not byte 6!
    uint8_t sendData[16] = {
        0xAA,   // [0x00] Start byte
        0xC3,   // [0x01] Command type
        0x00,   // [0x02]
        0x00,   // [0x03]
        0x80,   // [0x04]
        0x00,   // [0x05]
        0x00,   // [0x06]
        0x00,   // [0x07] Fan (filled in before send)
        0x00,   // [0x08] Temp (filled in before send)
        0x00,   // [0x09] Timer 1
        0x00,   // [0x0A] Timer 2
        0x00,   // [0x0B] Mode (filled in before send)
        0x00,   // [0x0C]
        0x3C,   // [0x0D]
        0x00,   // [0x0E] CRC (calculated before send)
        0x55    // [0x0F] End byte
    };
    
    // Queued command buffer
    uint8_t waitSendData[16] = {
        0xAA, 0xC3, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x55
    };
    
    // Query packet (constant - used to poll status)
    const uint8_t queryData[16] = {
        0xAA,   // [0] Start byte
        0xC0,   // [1] Query type
        0x00,   // [2]
        0x00,   // [3]
        0x80,   // [4]
        0x00,   // [5]
        0x00,   // [6]
        0x00,   // [7]
        0x00,   // [8]
        0x00,   // [9]
        0x00,   // [10]
        0x00,   // [11]
        0x00,   // [12]
        0x3F,   // [13]
        0x81,   // [14] CRC
        0x55    // [15] End byte
    };
    
    // Response validation header
    const uint8_t checkData[6] = {0xAA, 0xC0, 0x80, 0x00, 0x00, 0x00};
    
    // Helper methods
    const char* getModeString() {
        switch (opBytes) {
            case MODE_OFF:      return "Off";
            case MODE_AUTO:     return "Auto";
            case MODE_AUTO_ALT: return "Auto";  // Some units use 0x80
            case MODE_COOL:     return "Cool";
            case MODE_DRY:      return "Dry";
            case MODE_HEAT:     return "Heat";
            case MODE_FAN_ONLY: return "Fan Only";
            default:            return "Unknown";
        }
    }
    
    const char* getFanString() {
        switch (fanBytes) {
            case FAN_AUTO:       return "Auto";
            case FAN_HIGH:       return "High";
            case FAN_MEDIUM:     return "Medium";
            case FAN_MEDIUM_LOW: return "Medium-Low";
            case FAN_LOW:        return "Low";
            default:             return "Unknown";
        }
    }
    
} xyeState;
