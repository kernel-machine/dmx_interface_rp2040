#include "enttec_pro.h"

// =============================================================================
// Constructor
// =============================================================================

EnttecPro::EnttecPro()
    : m_state(STATE_WAIT_START)
    , m_label(0)
    , m_dataLength(0)
    , m_dataIndex(0)
{
    // Default widget parameters
    m_params.fwVersionLSB = FW_VERSION_LSB;
    m_params.fwVersionMSB = FW_VERSION_MSB;
    m_params.breakTime    = DEFAULT_BREAK_TIME;
    m_params.mabTime      = DEFAULT_MAB_TIME;
    m_params.refreshRate  = DEFAULT_REFRESH_RATE;

    // Fixed serial number (unique per device — could derive from chip ID)
    m_serialNumber[0] = 0x50;  // 'P'
    m_serialNumber[1] = 0x49;  // 'I'
    m_serialNumber[2] = 0x43;  // 'C'
    m_serialNumber[3] = 0x4F;  // 'O'

    // Initialize both universes to 0
    memset(m_dmxBuffer[0], 0, MAX_DMX_CHANNELS);
    memset(m_dmxBuffer[1], 0, MAX_DMX_CHANNELS);
    m_dmxLength[0] = 0;
    m_dmxLength[1] = 0;
    m_newDmxData[0] = false;
    m_newDmxData[1] = false;

    memset(m_dataBuffer, 0, sizeof(m_dataBuffer));
}

// =============================================================================
// Initialize
// =============================================================================

void EnttecPro::begin() {
    // Nothing to initialize beyond what the constructor does.
    // USB Serial is initialized separately in main.cpp
}

// =============================================================================
// Process a single byte from USB Serial (state machine parser)
// =============================================================================

void EnttecPro::processByte(uint8_t byte) {
    switch (m_state) {

    case STATE_WAIT_START:
        if (byte == ENTTEC_START_OF_MSG) {
            m_state = STATE_READ_LABEL;
        }
        break;

    case STATE_READ_LABEL:
        m_label = byte;
        m_state = STATE_READ_LEN_LSB;
        break;

    case STATE_READ_LEN_LSB:
        m_dataLength = byte;
        m_state = STATE_READ_LEN_MSB;
        break;

    case STATE_READ_LEN_MSB:
        m_dataLength |= (uint16_t)byte << 8;
        m_dataIndex = 0;

        if (m_dataLength == 0) {
            // No data payload, go straight to waiting for end
            m_state = STATE_WAIT_END;
        } else if (m_dataLength > MAX_ENTTEC_DATA) {
            // Invalid length, reset
            m_state = STATE_WAIT_START;
        } else {
            m_state = STATE_READ_DATA;
        }
        break;

    case STATE_READ_DATA:
        if (m_dataIndex < MAX_ENTTEC_DATA) {
            m_dataBuffer[m_dataIndex] = byte;
        }
        m_dataIndex++;

        if (m_dataIndex >= m_dataLength) {
            m_state = STATE_WAIT_END;
        }
        break;

    case STATE_WAIT_END:
        if (byte == ENTTEC_END_OF_MSG) {
            // Complete valid message received — handle it
            handleMessage();
        }
        // Reset parser regardless (even if end byte was wrong)
        m_state = STATE_WAIT_START;
        break;
    }
}

// =============================================================================
// Getters and Setters for universes (universe index 0 or 1)
// =============================================================================

bool EnttecPro::hasDmxData(uint8_t universe) const {
    if (universe > 1) return false;
    return m_newDmxData[universe];
}

const uint8_t* EnttecPro::getDmxData(uint8_t universe) const {
    if (universe > 1) return nullptr;
    return m_dmxBuffer[universe];
}

uint16_t EnttecPro::getDmxLength(uint8_t universe) const {
    if (universe > 1) return 0;
    return m_dmxLength[universe];
}

void EnttecPro::clearDmxFlag(uint8_t universe) {
    if (universe > 1) return;
    m_newDmxData[universe] = false;
}

// =============================================================================
// Send reply message back to PC via USB Serial
// =============================================================================

void EnttecPro::sendReply(uint8_t label, const uint8_t* data, uint16_t length) {
    uint8_t header[4];
    header[0] = ENTTEC_START_OF_MSG;
    header[1] = label;
    header[2] = length & 0xFF;          // Length LSB
    header[3] = (length >> 8) & 0xFF;   // Length MSB

    Serial.write(header, 4);

    if (length > 0 && data != nullptr) {
        Serial.write(data, length);
    }

    uint8_t footer = ENTTEC_END_OF_MSG;
    Serial.write(&footer, 1);
    Serial.flush();
}

// =============================================================================
// Message Router
// =============================================================================

void EnttecPro::handleMessage() {
    switch (m_label) {
    case LABEL_GET_PARAMS:
        handleGetParams();
        break;

    case LABEL_SET_PARAMS:
        handleSetParams();
        break;

    case LABEL_SEND_DMX:
        handleSendDmx(0); // Universe 1 (Port 1)
        break;

    case LABEL_SEND_DMX2:
        handleSendDmx(1); // Universe 2 (Port 2)
        break;

    case LABEL_PORT_ASSIGN:
        handlePortAssign();
        break;

    case LABEL_GET_SERIAL:
        handleGetSerial();
        break;

    case LABEL_GET_MANUFACTURER:
        handleGetManufacturer();
        break;

    case LABEL_GET_DEVICE_NAME:
        handleGetDeviceName();
        break;

    default:
        // Unknown label — silently ignore (per Enttec spec)
        break;
    }
}

// =============================================================================
// Label 3 — Get Widget Parameters (Reply)
// =============================================================================

void EnttecPro::handleGetParams() {
    // Reply format: FW_LSB, FW_MSB, BreakTime, MABTime, RefreshRate
    uint8_t reply[5];
    reply[0] = m_params.fwVersionLSB;
    reply[1] = m_params.fwVersionMSB;
    reply[2] = m_params.breakTime;
    reply[3] = m_params.mabTime;
    reply[4] = m_params.refreshRate;

    sendReply(LABEL_GET_PARAMS, reply, sizeof(reply));
}

// =============================================================================
// Label 4 — Set Widget Parameters
// =============================================================================

void EnttecPro::handleSetParams() {
    if (m_dataLength >= 5) {
        m_params.breakTime   = m_dataBuffer[2];
        m_params.mabTime     = m_dataBuffer[3];
        m_params.refreshRate = m_dataBuffer[4];
    }
}

// =============================================================================
// Label 6 & 0xA9 — Send DMX Packet (Port 1 & Port 2)
// =============================================================================

void EnttecPro::handleSendDmx(uint8_t universe) {
    if (universe > 1) return;
    if (m_dataLength < 1) return;

    // Skip the start code byte (byte 0), copy channel data
    uint16_t channelCount = m_dataLength - 1;
    if (channelCount > MAX_DMX_CHANNELS) {
        channelCount = MAX_DMX_CHANNELS;
    }

    if (channelCount > 0) {
        memcpy(m_dmxBuffer[universe], &m_dataBuffer[1], channelCount);
    }

    // Zero out remaining channels if fewer than 512 were sent
    if (channelCount < MAX_DMX_CHANNELS) {
        memset(&m_dmxBuffer[universe][channelCount], 0, MAX_DMX_CHANNELS - channelCount);
    }

    m_dmxLength[universe] = channelCount;
    m_newDmxData[universe] = true;
}

// =============================================================================
// Label 0xCB — Port Assignment Request
// Configures Port direction on Mk2 widgets. We respond with a mock success packet.
// =============================================================================

void EnttecPro::handlePortAssign() {
    // Pro Mk2 port assignment response: usually just returns a status byte (0 = Success)
    uint8_t status = 0;
    sendReply(LABEL_PORT_ASSIGN, &status, 1);
}

// =============================================================================
// Label 10 — Get Widget Serial Number (Reply)
// =============================================================================

void EnttecPro::handleGetSerial() {
    sendReply(LABEL_GET_SERIAL, m_serialNumber, 4);
}

// =============================================================================
// Label 77 (0x4D) — Get Device Manufacturer (Reply)
// =============================================================================

void EnttecPro::handleGetManufacturer() {
    // Reply format: ESTA_ID_LSB, ESTA_ID_MSB, then manufacturer string
    const char* name = "PicoDMX";
    uint16_t nameLen = strlen(name);
    uint16_t totalLen = 2 + nameLen;

    uint8_t reply[32];
    reply[0] = ESTA_MANUFACTURER_ID_LSB;
    reply[1] = ESTA_MANUFACTURER_ID_MSB;
    memcpy(&reply[2], name, nameLen);

    sendReply(LABEL_GET_MANUFACTURER, reply, totalLen);
}

// =============================================================================
// Label 78 (0x4E) — Get Device Name (Reply)
// =============================================================================

void EnttecPro::handleGetDeviceName() {
    // Reply format: DEV_ID_LSB, DEV_ID_MSB, then device name string
    const char* name = "Pico DMX Interface";
    uint16_t nameLen = strlen(name);
    uint16_t totalLen = 2 + nameLen;

    uint8_t reply[32];
    reply[0] = 0x01;  // Device ID LSB
    reply[1] = 0x00;  // Device ID MSB
    memcpy(&reply[2], name, nameLen);

    sendReply(LABEL_GET_DEVICE_NAME, reply, totalLen);
}
