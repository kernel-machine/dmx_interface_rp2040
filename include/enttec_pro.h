#ifndef ENTTEC_PRO_H
#define ENTTEC_PRO_H

#include <Arduino.h>

// =============================================================================
// Enttec DMX USB Pro Protocol Constants
// =============================================================================

// Frame delimiters
#define ENTTEC_START_OF_MSG  0x7E
#define ENTTEC_END_OF_MSG    0xE7

// Message Labels (PC → Widget)
#define LABEL_REPROGRAM_FW      1   // Reprogram firmware (not implemented)
#define LABEL_PROGRAM_FLASH     2   // Program flash page (not implemented)
#define LABEL_GET_PARAMS        3   // Get Widget Parameters (request/reply)
#define LABEL_SET_PARAMS        4   // Set Widget Parameters
#define LABEL_RECV_DMX          5   // Received DMX packet (widget → PC)
#define LABEL_SEND_DMX          6   // Output Only Send DMX Packet (Port 1)
#define LABEL_SEND_RDM          7   // Send RDM (not implemented)
#define LABEL_RECV_DMX_CHANGE   8   // Receive DMX on Change (not implemented)
#define LABEL_RECV_DMX_STATE    9   // Received DMX Change of State (not impl.)
#define LABEL_GET_SERIAL       10   // Get Widget Serial Number
#define LABEL_SEND_RDM_DISC    11   // Send RDM Discovery (not implemented)

// Enttec Pro Mk2 specific labels for multiple ports
#define LABEL_SEND_DMX2      0xA9   // Output Only Send DMX Packet (Port 2)
#define LABEL_PORT_ASSIGN    0xCB   // Port Assignment Request (to configure Mk2 ports)

// Extended labels (for manufacturer/device identification)
#define LABEL_GET_MANUFACTURER 77   // 0x4D - Get Device Manufacturer
#define LABEL_GET_DEVICE_NAME  78   // 0x4E - Get Device Name

// Maximum data payload size (DMX start code + 512 channels)
#define MAX_DMX_CHANNELS       512
#define MAX_ENTTEC_DATA        600

// Widget firmware version (emulated)
#define FW_VERSION_LSB         1
#define FW_VERSION_MSB         44   // v44.1 — mimics a real Pro widget

// Default widget parameters
#define DEFAULT_BREAK_TIME     9    // Break time in 10.67µs units (9 = ~96µs)
#define DEFAULT_MAB_TIME       1    // MAB time in 10.67µs units (1 = ~10.67µs)
#define DEFAULT_REFRESH_RATE   40   // DMX refresh rate in Hz (0 = max speed)

// ESTA Manufacturer ID (custom, non-registered)
#define ESTA_MANUFACTURER_ID_LSB  0x7A  // Custom ID
#define ESTA_MANUFACTURER_ID_MSB  0x70  // Custom ID

// =============================================================================
// Parser State Machine
// =============================================================================

enum EnttecParserState {
    STATE_WAIT_START,       // Waiting for 0x7E
    STATE_READ_LABEL,       // Reading message label
    STATE_READ_LEN_LSB,     // Reading data length LSB
    STATE_READ_LEN_MSB,     // Reading data length MSB
    STATE_READ_DATA,        // Reading data payload
    STATE_WAIT_END          // Waiting for 0xE7
};

// =============================================================================
// Widget Parameters (stored in RAM, not persisted)
// =============================================================================

struct WidgetParams {
    uint8_t  fwVersionLSB;
    uint8_t  fwVersionMSB;
    uint8_t  breakTime;     // In 10.67µs units
    uint8_t  mabTime;       // In 10.67µs units
    uint8_t  refreshRate;   // In Hz (0 = fastest)
};

// =============================================================================
// Enttec Pro Protocol Handler Class
// =============================================================================

class EnttecPro {
public:
    EnttecPro();

    /// Initialize the protocol handler
    void begin();

    /// Process a single byte received from USB Serial.
    /// Call this for each byte read from Serial.
    void processByte(uint8_t byte);

    /// Check if new DMX data is available for a given universe (0 or 1)
    bool hasDmxData(uint8_t universe) const;

    /// Get pointer to the 512-byte DMX channel buffer for a given universe (0 or 1)
    const uint8_t* getDmxData(uint8_t universe) const;

    /// Get the number of DMX channels in the last received packet for a universe (0 or 1)
    uint16_t getDmxLength(uint8_t universe) const;

    /// Clear the new-data flag for a universe (0 or 1)
    void clearDmxFlag(uint8_t universe);

private:
    // Parser state
    EnttecParserState m_state;
    uint8_t  m_label;
    uint16_t m_dataLength;
    uint16_t m_dataIndex;
    uint8_t  m_dataBuffer[MAX_ENTTEC_DATA];

    // Widget parameters
    WidgetParams m_params;

    // DMX output buffers (universe 0 = Port 1, universe 1 = Port 2)
    uint8_t  m_dmxBuffer[2][MAX_DMX_CHANNELS];
    uint16_t m_dmxLength[2];
    bool     m_newDmxData[2];

    // Widget serial number (fixed)
    uint8_t  m_serialNumber[4];

    // Send a reply message back to the PC via USB Serial
    void sendReply(uint8_t label, const uint8_t* data, uint16_t length);

    // Message handlers
    void handleGetParams();
    void handleSetParams();
    void handleSendDmx(uint8_t universe);
    void handlePortAssign();
    void handleGetSerial();
    void handleGetManufacturer();
    void handleGetDeviceName();
    void handleMessage();
};

#endif // ENTTEC_PRO_H
