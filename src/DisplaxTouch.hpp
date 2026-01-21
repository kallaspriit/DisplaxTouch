#pragma once

#include <Arduino.h>
#include <functional>

/**
 * Represents a single touch point from the Displax touch sensor.
 *
 * Contains position, size, pressure information and frame dimensions for coordinate normalization.
 */
struct TouchPoint {
    uint8_t id;           // Unique touch point identifier (0-5)
    uint16_t x;           // X coordinate in sensor units (0 to frameWidth)
    uint16_t y;           // Y coordinate in sensor units (0 to frameHeight)
    uint8_t width;        // Touch contact width
    uint8_t height;       // Touch contact height
    uint16_t pressure;    // Touch pressure value
    uint16_t frameWidth;  // Sensor frame width for coordinate normalization
    uint16_t frameHeight; // Sensor frame height for coordinate normalization
    bool active;          // True if touch is currently active
};

/**
 * Log message severity level.
 */
enum class TouchLogLevel {
    Info, // Informational message
    Warn  // Warning message
};

/**
 * Callback function type for touch events.
 *
 * @param touches Array of active touch points
 * @param count Number of active touches in the array
 */
using TouchCallback = std::function<void(const TouchPoint* touches, uint8_t count)>;

/**
 * Callback function type for log messages.
 *
 * @param level Message severity level
 * @param message Log message text
 */
using TouchLogCallback = std::function<void(TouchLogLevel level, const char* message)>;

/**
 * Touch sensor connection and synchronization state.
 *
 * State machine progression:
 * DISCONNECTED → INITIALIZING → CONNECTED → SYNCHRONIZING → SYNCHRONIZED
 *
 * Or on error:
 * INITIALIZING → INITIALIZATION_FAILED (timeout after 1000ms)
 */
enum class TouchState {
    DISCONNECTED,          // Initial state before begin() called
    INITIALIZING,          // Waiting for RESET response from sensor
    INITIALIZATION_FAILED, // No response from sensor within timeout period
    CONNECTED,             // Sensor responded to RESET, initialization complete
    SYNCHRONIZING,         // Searching for frame header (error recovery mode)
    SYNCHRONIZED,          // Processing touch frames normally
};

/**
 * Driver for Displax Zeeto touch controller over UART.
 *
 * Handles communication with the Displax touch sensor, including:
 * - Connection detection and initialization
 * - Touch event processing with CRC validation
 * - Multi-touch support (up to 6 simultaneous touches)
 * - Automatic frame synchronization and error recovery
 *
 * @note Requires UART stream at 115200 baud
 * @note Default frame size is 1050x650mm (updated automatically if sensor responds)
 *
 * Example usage:
 *
 * @code
 * DisplaxTouch touch(Stream2);
 *
 * void setup() {
 *     touch.setStateChangeCallback([](TouchState newState, TouchState previousState) {
 *         if (newState == TouchState::CONNECTED) {
 *             Stream.println("Touch sensor connected");
 *         }
 *     });
 *
 *     touch.addTouchListener([](const TouchPoint* touches, uint8_t count) {
 *         for (uint8_t i = 0; i < count; i++) {
 *             Stream.printf("Touch %d at (%d, %d)\n", touches[i].id, touches[i].x, touches[i].y);
 *         }
 *     });
 *
 *     touch.begin();
 * }
 *
 * void loop() {
 *     touch.loop();
 * }
 * @endcode
 */
class DisplaxTouch {
  public:
    /**
     * Callback function type for state change notifications.
     *
     * @param newState The new state being entered
     * @param previousState The state being exited
     */
    using StateChangeCallback = std::function<void(TouchState newState, TouchState previousState)>;

    /**
     * Constructs a DisplaxTouch instance.
     *
     * @param stream Stream (usually HardwareStream) for communication with touch sensor
     */
    DisplaxTouch(Stream& stream);

    /**
     * Initializes the touch sensor and starts the connection sequence.
     *
     * Sends RESET command and waits for sensor response. State changes to INITIALIZING.
     * If no response within 1000ms, state changes to INITIALIZATION_FAILED.
     *
     * @note Call this once in setup()
     */
    void begin();

    /**
     * Processes incoming UART data and touch events.
     *
     * Must be called regularly in the main loop to process touch events and maintain
     * connection state. Handles timeout detection, frame synchronization, and touch
     * event dispatching to registered listeners.
     *
     * @note Call this every loop iteration
     */
    void loop();

    /**
     * Gets the current number of active touches.
     *
     * @return Number of active touch points (0-6)
     */
    uint8_t getTouchCount() const;

    /**
     * Gets a specific touch point by index.
     *
     * @param index Touch point index (0 to getTouchCount()-1)
     * @return Reference to the TouchPoint at the specified index
     * @warning No bounds checking - ensure index < getTouchCount()
     */
    const TouchPoint& getTouch(uint8_t index) const;

    /**
     * Gets the scan time from the last touch report.
     *
     * @return Scan time in sensor-specific units
     */
    uint16_t getScanTime() const;

    /**
     * Checks if any touch is currently active.
     *
     * @return True if at least one touch is active, false otherwise
     */
    bool isTouched() const;

    /**
     * Clears the current touch state.
     *
     * Call this after consuming touch events to prevent re-processing the same touches.
     * Does not affect listener callbacks.
     */
    void clearTouches();

    /**
     * Adds a callback listener for touch events
     *
     * The callback is invoked whenever new touch data arrives from the sensor.
     * Multiple listeners can be registered (up to MAX_LISTENERS).
     *
     * @param callback Function to call when touch events occur
     * @return Unique listener ID (>= 0) on success, -1 if max listeners reached or callback is null
     */
    int addTouchListener(TouchCallback callback);

    /**
     * Removes a previously registered touch listener.
     *
     * @param listenerId The listener ID returned by addTouchListener()
     * @return True if listener was found and removed, false otherwise
     */
    bool removeTouchListener(int listenerId);

    /**
     * Sets the log message callback.
     *
     * Enables logging of internal library events. Disabled by default.
     *
     * @param callback Function to receive log messages, or nullptr to disable logging
     */
    void setLogCallback(TouchLogCallback callback);

    /**
     * Gets the current touch sensor connection state.
     *
     * @return Current TouchState value
     */
    TouchState getTouchState() const;

    /**
     * Sets the state change notification callback.
     *
     * The callback is invoked whenever the sensor connection state changes.
     * Useful for detecting connection/disconnection events.
     *
     * @param callback Function to call on state changes, or nullptr to disable
     */
    void setStateChangeCallback(StateChangeCallback callback);

    /**
     * Gets the current frame width.
     *
     * @return Frame width in millimeters (default: 1050)
     */
    uint16_t getFrameWidth() const;

    /**
     * Gets the current frame height.
     *
     * @return Frame height in millimeters (default: 650)
     */
    uint16_t getFrameHeight() const;

    /**
     * Manually sets the frame dimensions.
     *
     * Overrides the default frame size and any size received from the sensor.
     * Useful when the sensor fails to respond with frame size during initialization.
     *
     * @param width Frame width in millimeters
     * @param height Frame height in millimeters
     */
    void setFrameSize(uint16_t width, uint16_t height);

  private:
    /**
     * Displax UART protocol command codes.
     *
     * Commands are sent as little-endian 16-bit values.
     * Response IDs match the command code except RESET which responds with 0x226E.
     */
    enum class Command : uint16_t {
        RESET = 0x0000,                      // Reset sensor (responds with RESET_RESPONSE)
        GET_HID_DESCRIPTOR = 0x0001,         // Request HID descriptor
        GET_HID_REPORT_DESCRIPTION = 0x0002, // Request HID report descriptor
        GET_FRAME_SIZE = 0x0003,             // Request sensor frame dimensions
        TOUCH_REPORT_ID = 0x0004,            // Touch frame report ID (incoming data)
        ENABLE_REPORTING = 0x0005,           // Enable touch event Streaming
        DISABLE_REPORTING = 0x0006,          // Disable touch event Streaming
        RESET_RESPONSE = 0x226E,             // Reset command response ID
        DISABLE_USB_REPORTING = 0xFF00,      // Disable USB touch reporting
        ENABLE_USB_REPORTING = 0xFF01,       // Enable USB touch reporting
    };

    // Constants
    static constexpr size_t RX_BUFFER_SIZE = 2048;                   // Stream receive buffer size
    static constexpr size_t TOUCH_REPORT_SIZE = 72;                  // Touch frame total size (4 header + 64 payload + 4 CRC)
    static constexpr size_t TOUCH_CRC_SIZE = 4;                      // CRC32 field size in bytes
    static constexpr size_t GET_HID_DESCRIPTION_SIZE = 32;           // HID descriptor response size
    static constexpr size_t GET_HID_REPORT_DESCRIPTION_SIZE = 708;   // HID report descriptor response size
    static constexpr size_t GET_FRAME_SIZE_SIZE = 6;                 // Frame size response size
    static constexpr size_t TOUCH_PAYLOAD_SIZE = 64;                 // Touch report payload size
    static constexpr size_t MAX_TOUCH_CONTACTS = 6;                  // Maximum touch contacts in protocol
    static constexpr size_t MAX_TOUCHES = 6;                         // Maximum simultaneous touch points supported
    static constexpr size_t MAX_LISTENERS = 4;                       // Maximum number of touch event listeners
    static constexpr size_t LOG_BUFFER_SIZE = 128;                   // Log message buffer size
    static constexpr unsigned long INITIALIZATION_TIMEOUT_MS = 1000; // Sensor initialization timeout

    // CRC32 lookup table for nibble-based calculation (Ethernet polynomial 0x04C11DB7)
    static const uint32_t CRC32_TABLE[16];

    // Dependencies
    Stream& stream; // Stream for sensor communication

    // State
    TouchState state = TouchState::DISCONNECTED; // Current connection/synchronization state
    uint8_t rxBuffer[RX_BUFFER_SIZE];            // Stream receive buffer
    size_t rxBufferSize = 0;                     // Current position in receive buffer
    TouchPoint touches[MAX_TOUCHES] = {};        // Array of active touch points
    uint8_t touchCount = 0;                      // Current number of active touches
    uint16_t frameWidth = 1050;                  // Sensor frame width (default 1050mm)
    uint16_t frameHeight = 650;                  // Sensor frame height (default 650mm)
    TouchCallback listeners[MAX_LISTENERS] = {}; // Array of registered touch listeners
    uint8_t listenerCount = 0;                   // Number of registered listeners
    int listenerIds[MAX_LISTENERS] = {};         // Unique IDs for registered listeners
    int nextListenerId = 0;                      // Next listener ID to assign

    // Callbacks
    StateChangeCallback stateChangeCallback = nullptr; // State change notification callback
    TouchLogCallback logCallback = nullptr;            // Log message callback

    // Timing
    unsigned long initializingStartTimeMs = 0; // Initialization start time for timeout detection
    unsigned long scanTime = 0;                // Last reported scan time

    //==========================================================================
    // Logging
    //==========================================================================

    /**
     * Logs an informational message.
     *
     * @param format Printf-style format string
     * @param ... Variable arguments for format string
     */
    void log(const char* format, ...);

    /**
     * Logs a warning message.
     *
     * @param format Printf-style format string
     * @param ... Variable arguments for format string
     */
    void warn(const char* format, ...);

    //==========================================================================
    // Sending commands
    //==========================================================================

    /** Sends RESET command and transitions to INITIALIZING state. */
    void sendReset();

    /** Sends GET_HID_DESCRIPTOR command. */
    void sendGetHIDDescriptor();

    /** Sends GET_HID_REPORT_DESCRIPTION command. */
    void sendGetHIDReportDescription();

    /** Sends GET_FRAME_SIZE command to request sensor dimensions. */
    void sendGetFrameSize();

    /** Sends ENABLE_REPORTING command to start touch event Streaming. */
    void sendEnableReporting();

    /** Sends DISABLE_REPORTING command to stop touch event Streaming. */
    void sendDisableReporting();

    /** Sends DISABLE_USB_REPORTING command. */
    void sendDisableUsbReporting();

    /** Sends ENABLE_USB_REPORTING command. */
    void sendEnableUsbReporting();

    /**
     * Sends a command to the sensor over UART.
     *
     * @param command The command code to send
     */
    void sendCommand(Command command);

    /**
     * Changes the current state and notifies callbacks.
     *
     * @param newState The new state to transition to
     */
    void setState(TouchState newState);

    //==========================================================================
    // Frame Synchronization
    //==========================================================================

    /**
     * Searches for touch frame header pattern in buffer.
     *
     * @param data Buffer to search
     * @param length Length of buffer
     * @return Offset of frame header if found, -1 otherwise
     */
    int findFrameHeader(uint8_t* data, size_t length);

    /**
     * Validates touch frame header pattern.
     *
     * @param data Buffer containing potential touch frame
     * @param length Length of buffer
     * @return True if valid touch frame header present (04 00 40 00)
     */
    bool isValidTouchFrame(uint8_t* data, size_t length);

    /**
     * Searches for frame header and synchronizes to it.
     *
     * Called when out of sync (SYNCHRONIZING state). Discards bytes until
     * valid frame header found, then transitions to SYNCHRONIZED state.
     */
    void synchronize();

    /**
     * Consumes (removes) bytes from the receive buffer.
     *
     * @param count Number of bytes to remove from the beginning of the buffer
     */
    void consumeBuffer(size_t count);

    //==========================================================================
    // CRC Validation
    //==========================================================================

    /**
     * Calculates CRC32 checksum using nibble-based lookup table.
     *
     * @param data Data to calculate CRC over
     * @param length Length of data (must be multiple of 4)
     * @return Calculated CRC32 value
     */
    static uint32_t calculateCRC32(const uint8_t* data, size_t length);

    /**
     * Verifies CRC32 of a touch frame.
     *
     * @param frame Complete 72-byte touch frame
     * @return True if calculated CRC matches stored CRC in frame
     */
    bool verifyCRC(const uint8_t* frame);

    //==========================================================================
    // Data Processing
    //==========================================================================

    /**
     * Reads available data from stream into buffer.
     *
     * Called from loop(). Handles timeout checking, buffer overflow protection,
     * and dispatches to appropriate state handler.
     */
    void readStreamData();

    /**
     * Processes buffered Stream data and dispatches to command handlers.
     *
     * @param data Buffer containing received data
     * @param length Length of data in buffer
     */
    void processStreamData(uint8_t* data, size_t length);

    /**
     * Processes GET_HID_DESCRIPTOR response.
     *
     * @param data Response data buffer
     * @param length Length of response
     */
    void processGetHidDescriptor(uint8_t* data, size_t length);

    /**
     * Processes GET_HID_REPORT_DESCRIPTION response.
     *
     * @param data Response data buffer
     * @param length Length of response
     */
    void processGetHidReportDescriptor(uint8_t* data, size_t length);

    /**
     * Processes GET_FRAME_SIZE response and stores frame dimensions.
     *
     * @param data Response data buffer (contains width and height)
     * @param length Length of response
     */
    void processGetFrameSize(uint8_t* data, size_t length);

    /**
     * Processes touch report frame with CRC validation.
     *
     * Validates frame header and CRC, extracts touch points, updates internal
     * state, and notifies all registered listeners.
     *
     * @param data Touch frame buffer (72 bytes)
     * @param length Length of frame
     */
    void processTouchReport(uint8_t* data, size_t length);

    /**
     * Processes ENABLE_REPORTING response.
     *
     * @param data Response data buffer
     * @param length Length of response
     */
    void processEnableReporting(uint8_t* data, size_t length);

    /**
     * Processes DISABLE_REPORTING response.
     *
     * @param data Response data buffer
     * @param length Length of response
     */
    void processDisableReporting(uint8_t* data, size_t length);

    /**
     * Processes RESET response and marks sensor as connected.
     *
     * RESET response proves sensor is responsive. Marks sensor as CONNECTED
     * and continues initialization sequence.
     *
     * @param data Response data buffer
     * @param length Length of response
     */
    void processResetResponse(uint8_t* data, size_t length);

    /**
     * Processes DISABLE_USB_REPORTING response.
     *
     * @param data Response data buffer
     * @param length Length of response
     */
    void processDisableUsbReporting(uint8_t* data, size_t length);

    /**
     * Processes ENABLE_USB_REPORTING response.
     *
     * @param data Response data buffer
     * @param length Length of response
     */
    void processEnableUsbReporting(uint8_t* data, size_t length);

    //==========================================================================
    // Utilities
    //==========================================================================

    /**
     * Converts command code to human-readable name.
     *
     * @param command Command code
     * @return Command name as String
     */
    static inline String getCommandName(Command command);

    /**
     * Converts touch state to human-readable name.
     *
     * @param state Touch state value
     * @return State name as String
     */
    static inline String getStateName(TouchState state);

    /**
     * Converts numeric ID to hex string.
     *
     * @param id Numeric value
     * @return Hex string (e.g., "0x0004")
     */
    static inline String idToHex(unsigned long id);

    /**
     * Converts buffer to hex dump string for debugging.
     *
     * @param buffer Data buffer
     * @param length Length of buffer
     * @param name Optional name prefix
     * @return Hex dump string
     */
    static inline String bufferToHex(const uint8_t* buffer, uint8_t length, String name = "");
};