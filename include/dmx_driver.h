#ifndef DMX_DRIVER_H
#define DMX_DRIVER_H

#include <Arduino.h>
// Include directly from the Pico-DMX library
#include <DmxOutput.h> 

// Port 1 Pin definitions (pio0)
#define DMX1_TX_PIN      0   // GP0 — Output pin for Port 1
#define DMX1_DE_PIN      1   // GP1 — Direction Enable pin for Port 1

// Port 2 Pin definitions (pio1)
#define DMX2_TX_PIN      4   // GP4 — Output pin for Port 2
#define DMX2_DE_PIN      5   // GP5 — Direction Enable pin for Port 2

// DMX512 timing constants
#define DMX_CHANNELS_MAX    512     // Maximum DMX channels
#define DMX_DEFAULT_FPS     40      // Default refresh rate (frames per second)

// =============================================================================
// DMX Driver Class (wrapper around PicoDMX)
// =============================================================================

class DmxDriver {
public:
    DmxDriver();
    ~DmxDriver();

    /// Initialize PIO blocks and GPIOs for both DMX outputs
    void begin();

    /// Set the DMX channel data to transmit for a given port (0 or 1).
    /// @param port    Port index (0 = Port 1, 1 = Port 2)
    /// @param data    Pointer to channel data (channel 1 at index 0)
    /// @param length  Number of channels (1–512)
    void setData(uint8_t port, const uint8_t* data, uint16_t length);

    /// Transmit one complete DMX frame on a given port (0 or 1).
    void sendFrame(uint8_t port);

    /// Set the refresh rate (frames per second, 0 = max speed)
    void setRefreshRate(uint8_t fps);

    /// Check if it's time to send the next frames based on refresh rate.
    /// If so, sends the frames automatically.
    void update();

private:
    // Pointers to the Pico-DMX DmxOutput engines
    ::DmxOutput* m_dmxEngine[2];

    // Buffer to hold DMX data.
    // The PicoDMX library requires 513 bytes (Index 0 = Start Code = 0)
    uint8_t  m_buffer[2][DMX_CHANNELS_MAX + 1];

    uint16_t m_length[2];                     // Number of active channels per port
    uint8_t  m_refreshRate;                  // Target FPS
    unsigned long m_lastFrameTime;           // micros() of last frame
    unsigned long m_frameInterval;           // Interval between frames in µs
};

#endif // DMX_DRIVER_H
