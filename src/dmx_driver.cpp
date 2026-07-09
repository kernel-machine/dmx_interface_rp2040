#include "dmx_driver.h"

// =============================================================================
// Constructor
// =============================================================================

DmxDriver::DmxDriver()
    : m_refreshRate(DMX_DEFAULT_FPS)
    , m_lastFrameTime(0)
    , m_frameInterval(25000)  // 40 FPS default = 25000µs
{
    // Instantiate PicoDMX engines
    m_dmxEngine[0] = new ::DmxOutput();
    m_dmxEngine[1] = new ::DmxOutput();

    // Initialize buffers (index 0 is the start code, standard is 0)
    memset(m_buffer[0], 0, DMX_CHANNELS_MAX + 1);
    memset(m_buffer[1], 0, DMX_CHANNELS_MAX + 1);

    m_length[0] = DMX_CHANNELS_MAX;
    m_length[1] = DMX_CHANNELS_MAX;
}

DmxDriver::~DmxDriver() {
    delete m_dmxEngine[0];
    delete m_dmxEngine[1];
}

// =============================================================================
// Initialize PIO blocks and GPIOs
// =============================================================================

void DmxDriver::begin() {
    // Pin DE setup for both RS485 transceivers (keep HIGH to enable TX)
    pinMode(DMX1_DE_PIN, OUTPUT);
    digitalWrite(DMX1_DE_PIN, HIGH);

    pinMode(DMX2_DE_PIN, OUTPUT);
    digitalWrite(DMX2_DE_PIN, HIGH);

    // Initialize PicoDMX Port 1 on pio0
    m_dmxEngine[0]->begin(DMX1_TX_PIN, pio0);

    // Initialize PicoDMX Port 2 on pio1 (using pio1 to avoid state machine conflicts)
    m_dmxEngine[1]->begin(DMX2_TX_PIN, pio1);

    setRefreshRate(m_refreshRate);
}

// =============================================================================
// Set DMX channel data for a given port
// =============================================================================

void DmxDriver::setData(uint8_t port, const uint8_t* data, uint16_t length) {
    if (port > 1) return;
    if (length > DMX_CHANNELS_MAX) {
        length = DMX_CHANNELS_MAX;
    }
    
    // Copy data starting from index 1. Index 0 remains 0 (Start Code)
    memcpy(&m_buffer[port][1], data, length);
    m_length[port] = length;
}

// =============================================================================
// Set refresh rate
// =============================================================================

void DmxDriver::setRefreshRate(uint8_t fps) {
    m_refreshRate = fps;
    if (fps == 0) {
        m_frameInterval = 0;
    } else {
        m_frameInterval = 1000000UL / fps;
    }
}

// =============================================================================
// Send a complete DMX frame on a given port
// =============================================================================

void DmxDriver::sendFrame(uint8_t port) {
    if (port > 1) return;

    // Send packet using PicoDMX PIO engine (non-blocking call)
    // We send m_length[port] + 1 (data channels + 1 start code byte)
    m_dmxEngine[port]->write(m_buffer[port], m_length[port] + 1);
}

// =============================================================================
// Periodic update — call from loop()
// =============================================================================

void DmxDriver::update() {
    unsigned long now = micros();

    if (m_frameInterval == 0 || (now - m_lastFrameTime) >= m_frameInterval) {
        // Transmit frames on both outputs
        sendFrame(0);
        sendFrame(1);
        m_lastFrameTime = now;
    }
}
