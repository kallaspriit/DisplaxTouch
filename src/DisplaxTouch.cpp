#include "DisplaxTouch.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

const uint32_t DisplaxTouch::CRC32_TABLE[16] = {
    0x00000000, 0x04C11DB7, 0x09823B6E, 0x0D4326D9, 0x130476DC, 0x17C56B6B, 0x1A864DB2, 0x1E475005, 0x2608EDB8, 0x22C9F00F, 0x2F8AD6D6, 0x2B4BCB61, 0x350C9B64, 0x31CD86D3, 0x3C8EA00A, 0x384FBDBD};

DisplaxTouch::DisplaxTouch(Stream& stream)
    : stream(stream) {
    // Initialize RX buffer
    memset(rxBuffer, 0, sizeof(rxBuffer));
}

void DisplaxTouch::begin() {
    log("Initializing");

    // Track initialization start time for timeout detection
    initializingStartTimeMs = millis();

    // Flush any possibly queued stream data
    while (stream.available()) {
        stream.read();
    }

    // Send reset command
    sendReset();
}

void DisplaxTouch::loop() {
    // Check for initialization timeout
    if (state == TouchState::INITIALIZING && initializingStartTimeMs > 0) {
        unsigned long currentMs = millis();

        if (currentMs - initializingStartTimeMs >= INITIALIZATION_TIMEOUT_MS) {
            warn("Initialization timeout - no response from sensor in %lu ms", INITIALIZATION_TIMEOUT_MS);
            setState(TouchState::INITIALIZATION_FAILED);

            // Reset to prevent repeated failures
            initializingStartTimeMs = 0;
        }
    }

    // Read and process stream data
    readStreamData();
}

void DisplaxTouch::sendCommand(Command command) {
    // Build command bytes in little-endian format
    uint16_t commandValue = static_cast<uint16_t>(command);
    uint8_t commandBytes[2] = {static_cast<uint8_t>(commandValue & 0xFF), static_cast<uint8_t>((commandValue >> 8) & 0xFF)};

    // Send command over stream
    stream.write(commandBytes, 2);
    stream.flush();

    // Log sent command
    log("Sent command: %s (%s)", getCommandName(command).c_str(), idToHex(static_cast<uint16_t>(command)).c_str());
}

void DisplaxTouch::consumeBuffer(size_t bytesToConsume) {
    // Clear entire buffer if consuming all or more bytes than available
    if (bytesToConsume >= rxBufferSize) {
        rxBufferSize = 0;

        return;
    }

    // Shift remaining data to beginning of buffer
    memmove(rxBuffer, rxBuffer + bytesToConsume, rxBufferSize - bytesToConsume);
    rxBufferSize -= bytesToConsume;
}

int DisplaxTouch::findFrameHeader(uint8_t* data, size_t length) {
    // Need at least 4 bytes to match header pattern
    if (length < 4) {
        return -1;
    }

    // Search for the 4-byte header pattern: 04 00 40 00
    for (size_t position = 0; position <= length - 4; position++) {
        bool headerFound = data[position] == 0x04 && data[position + 1] == 0x00 && data[position + 2] == 0x40 && data[position + 3] == 0x00;

        if (headerFound) {
            return static_cast<int>(position);
        }
    }

    return -1;
}

bool DisplaxTouch::isValidTouchFrame(uint8_t* data, size_t length) {
    // Check minimum length requirement
    if (length < TOUCH_REPORT_SIZE) {
        return false;
    }

    // Verify header pattern: 04 00 40 00
    bool validHeader = data[0] == 0x04 && data[1] == 0x00 && data[2] == 0x40 && data[3] == 0x00;

    return validHeader;
}

void DisplaxTouch::synchronize() {
    // Need at least 4 bytes to find header
    if (rxBufferSize < 4) {
        return;
    }

    // Attempt to find frame header
    int frameHeaderPosition = findFrameHeader(rxBuffer, rxBufferSize);

    if (frameHeaderPosition > 0) {
        // Found header, discard bytes before it
        log("Synchronizing: discarding %d bytes before header", frameHeaderPosition);

        consumeBuffer(frameHeaderPosition);

        setState(TouchState::SYNCHRONIZED);
    } else if (frameHeaderPosition == 0) {
        // Already at header, consider synchronized
        log("Synchronized at frame header");

        setState(TouchState::SYNCHRONIZED);
    } else {
        // No header found, discard entire buffer
        log("No header found, discarding buffer");

        consumeBuffer(rxBufferSize);
    }
}

uint32_t DisplaxTouch::calculateCRC32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    size_t wordCount = length / 4;

    // Process data in 32-bit words (length must be multiple of 4)
    for (size_t wordIndex = 0; wordIndex < wordCount; wordIndex++) {
        // Assemble 32-bit word from bytes (little-endian)
        size_t byteOffset = wordIndex * 4;
        uint32_t word = static_cast<uint32_t>(data[byteOffset]) | (static_cast<uint32_t>(data[byteOffset + 1]) << 8) | (static_cast<uint32_t>(data[byteOffset + 2]) << 16) |
                        (static_cast<uint32_t>(data[byteOffset + 3]) << 24);

        crc = crc ^ word;

        // Process 8 nibbles (32 bits) using the lookup table
        crc = (crc << 4) ^ CRC32_TABLE[crc >> 28];
        crc = (crc << 4) ^ CRC32_TABLE[crc >> 28];
        crc = (crc << 4) ^ CRC32_TABLE[crc >> 28];
        crc = (crc << 4) ^ CRC32_TABLE[crc >> 28];
        crc = (crc << 4) ^ CRC32_TABLE[crc >> 28];
        crc = (crc << 4) ^ CRC32_TABLE[crc >> 28];
        crc = (crc << 4) ^ CRC32_TABLE[crc >> 28];
        crc = (crc << 4) ^ CRC32_TABLE[crc >> 28];
    }

    return crc;
}

bool DisplaxTouch::verifyCRC(const uint8_t* frame) {
    // Calculate CRC over header + payload (68 bytes)
    uint32_t calculatedCrc = calculateCRC32(frame, TOUCH_REPORT_SIZE - TOUCH_CRC_SIZE);

    // Extract stored CRC from frame (little-endian, bytes 68-71)
    uint32_t storedCrc = static_cast<uint32_t>(frame[68]) | (static_cast<uint32_t>(frame[69]) << 8) | (static_cast<uint32_t>(frame[70]) << 16) | (static_cast<uint32_t>(frame[71]) << 24);

    return calculatedCrc == storedCrc;
}

void DisplaxTouch::sendReset() {
    setState(TouchState::INITIALIZING);

    // Send reset command
    sendCommand(Command::RESET);
}

void DisplaxTouch::sendGetHIDDescriptor() {
    sendCommand(Command::GET_HID_DESCRIPTOR);
}

void DisplaxTouch::sendGetHIDReportDescription() {
    sendCommand(Command::GET_HID_REPORT_DESCRIPTION);
}

void DisplaxTouch::sendGetFrameSize() {
    sendCommand(Command::GET_FRAME_SIZE);
}

void DisplaxTouch::sendEnableReporting() {
    sendCommand(Command::ENABLE_REPORTING);
}

void DisplaxTouch::sendDisableReporting() {
    sendCommand(Command::DISABLE_REPORTING);
}

void DisplaxTouch::sendDisableUsbReporting() {
    sendCommand(Command::DISABLE_USB_REPORTING);
}

void DisplaxTouch::sendEnableUsbReporting() {
    sendCommand(Command::ENABLE_USB_REPORTING);
}

void DisplaxTouch::setState(TouchState newState) {
    TouchState previousState = state;

    if (newState == previousState) {
        return;
    }

    state = newState;

    log("State changed from %s to %s", getStateName(previousState).c_str(), getStateName(newState).c_str());

    // Notify callback
    if (stateChangeCallback) {
        stateChangeCallback(newState, previousState);
    }
}

void DisplaxTouch::readStreamData() {
    static size_t lastRxBufferPos = 0;

    // Read all available data into RX buffer first (batch reading)
    while (stream.available() && rxBufferSize < RX_BUFFER_SIZE) {
        rxBuffer[rxBufferSize++] = stream.read();
    }

    // Log buffer if new data received (verbose, enable only for debugging)
    // if (rxBufferSize != lastRxBufferPos) {
    //     Stream.println(bufferToHex(rxBuffer, rxBufferSize, "RX Buffer"));

    //     lastRxBufferPos = rxBufferSize;
    // }

    // Buffer overflow protection
    if (rxBufferSize >= RX_BUFFER_SIZE) {
        warn("RX buffer overflow, resetting and searching for frame header");
        rxBufferSize = 0;

        setState(TouchState::SYNCHRONIZING);

        return;
    }

    // Need at least 2 bytes to determine report ID
    if (rxBufferSize < 2) {
        return;
    }

    // State machine for processing
    switch (state) {
        case TouchState::DISCONNECTED:
        case TouchState::CONNECTED:
        case TouchState::INITIALIZATION_FAILED:
        case TouchState::INITIALIZING:
        case TouchState::SYNCHRONIZED:
            processStreamData(rxBuffer, rxBufferSize);
            break;

        case TouchState::SYNCHRONIZING:
            // Error recovery: search for frame header
            synchronize();

            break;
    }
}

void DisplaxTouch::processStreamData(uint8_t* data, size_t length) {
    // Determine report ID
    Command reportId = static_cast<Command>(data[0] | (data[1] << 8));

    // Process based on report ID
    if (reportId == Command::GET_HID_DESCRIPTOR && length >= GET_HID_DESCRIPTION_SIZE) {
        processGetHidDescriptor(data, length);
    } else if (reportId == Command::GET_HID_REPORT_DESCRIPTION && length >= GET_HID_REPORT_DESCRIPTION_SIZE) {
        processGetHidReportDescriptor(data, length);
    } else if (reportId == Command::GET_FRAME_SIZE && length >= GET_FRAME_SIZE_SIZE) {
        processGetFrameSize(data, length);
    } else if (reportId == Command::TOUCH_REPORT_ID && length >= TOUCH_REPORT_SIZE) {
        processTouchReport(data, length);
    } else if (reportId == Command::ENABLE_REPORTING) {
        processEnableReporting(data, length);
    } else if (reportId == Command::DISABLE_REPORTING) {
        processDisableReporting(data, length);
    } else if (reportId == Command::RESET_RESPONSE) {
        processResetResponse(data, length);
    } else if (reportId == Command::DISABLE_USB_REPORTING) {
        processDisableUsbReporting(data, length);
    } else if (reportId == Command::ENABLE_USB_REPORTING) {
        processEnableUsbReporting(data, length);
    } else if (length >= TOUCH_REPORT_SIZE) {
        // Unknown report ID with enough data - likely out of sync
        warn("Unknown report %s, searching for frame header", idToHex(static_cast<uint16_t>(reportId)).c_str());

        setState(TouchState::SYNCHRONIZING);
    }
}

void DisplaxTouch::processGetHidDescriptor(uint8_t* data, size_t length) {
    log("Received HID descriptor (length: %zu)", length);

    consumeBuffer(GET_HID_DESCRIPTION_SIZE);
}

void DisplaxTouch::processGetHidReportDescriptor(uint8_t* data, size_t length) {
    log("Received HID report descriptor (length: %zu)", length);

    consumeBuffer(GET_HID_REPORT_DESCRIPTION_SIZE);
}

void DisplaxTouch::processGetFrameSize(uint8_t* data, size_t length) {
    // Extract frame width and height from response
    uint16_t receivedWidth = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);
    uint16_t receivedHeight = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);

    // Store frame size in class members
    frameWidth = receivedWidth;
    frameHeight = receivedHeight;

    log("Received frame size (width: %u, height: %u)", frameWidth, frameHeight);

    consumeBuffer(GET_FRAME_SIZE_SIZE);

    sendDisableUsbReporting();
}

void DisplaxTouch::processTouchReport(uint8_t* data, size_t length) {
    // Validate touch frame header before processing
    if (!isValidTouchFrame(data, length)) {
        warn("Invalid touch frame header, re-synchronizing");

        consumeBuffer(1);
        setState(TouchState::SYNCHRONIZING);

        return;
    }

    // Extract and validate payload size from header
    uint16_t payloadSize = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);

    if (payloadSize != TOUCH_PAYLOAD_SIZE) {
        warn("Unexpected touch report payload size: %u, expected: %zu", payloadSize, TOUCH_PAYLOAD_SIZE);

        consumeBuffer(1);
        setState(TouchState::SYNCHRONIZING);

        return;
    }

    // Verify CRC integrity
    if (!verifyCRC(data)) {
        warn("CRC mismatch, re-synchronizing");

        consumeBuffer(1);
        setState(TouchState::SYNCHRONIZING);

        return;
    }

    // Get pointer to payload (skip 4-byte header)
    uint8_t* payload = data + 4;

    // Extract touch count and scan time from payload
    // Payload structure (64 bytes):
    // - reportId: 1 byte at offset 0
    // - touches[6]: 60 bytes at offset 1-60
    // - touchCount: 1 byte at offset 61
    // - scanTime: 2 bytes at offset 62-63
    uint8_t reportedTouchCount = payload[61];
    scanTime = static_cast<uint16_t>(payload[62]) | (static_cast<uint16_t>(payload[63]) << 8);

    // Parse and store active touches
    touchCount = 0;

    for (size_t touchIndex = 0; touchIndex < reportedTouchCount && touchIndex < MAX_TOUCH_CONTACTS; touchIndex++) {
        const uint8_t* touchData = &payload[1 + touchIndex * 10];
        uint8_t touchStatus = touchData[0];

        // Skip inactive touch slots
        if (touchStatus == 0) {
            continue;
        }

        // Store active touch point
        TouchPoint& point = touches[touchCount++];
        point.id = touchData[1];
        point.x = static_cast<uint16_t>(touchData[2]) | (static_cast<uint16_t>(touchData[3]) << 8);
        point.y = static_cast<uint16_t>(touchData[4]) | (static_cast<uint16_t>(touchData[5]) << 8);
        point.width = touchData[6];
        point.height = touchData[7];
        point.pressure = static_cast<uint16_t>(touchData[8]) | (static_cast<uint16_t>(touchData[9]) << 8);
        point.active = true;
        point.frameWidth = frameWidth;
        point.frameHeight = frameHeight;
    }

    // Notify all registered listeners
    for (uint8_t listenerIndex = 0; listenerIndex < listenerCount; listenerIndex++) {
        listeners[listenerIndex](touches, touchCount);
    }

    // Consume processed touch report
    consumeBuffer(TOUCH_REPORT_SIZE);
}

void DisplaxTouch::processEnableReporting(uint8_t* data, size_t length) {
    log("Received enable reporting response");

    // Consume the response
    consumeBuffer(2);

    // Transition to synchronized state
    setState(TouchState::SYNCHRONIZED);
}

void DisplaxTouch::processDisableReporting(uint8_t* data, size_t length) {
    log("Received disable reporting (length: %zu)", length);

    // Consume the response
    consumeBuffer(2);
}

void DisplaxTouch::processResetResponse(uint8_t* data, size_t length) {
    // Measure initialization time
    unsigned long initializationTimeTakenMs = millis() - initializingStartTimeMs;

    log("Received reset response (initialization time: %lu ms)", initializationTimeTakenMs);

    // Consume the response
    consumeBuffer(2);

    // Mark as connected - RESET response proves sensor is responsive
    setState(TouchState::CONNECTED);
    initializingStartTimeMs = 0; // Clear timeout tracking

    // Try to continue full initialization sequence
    // If sensor is already powered, these may fail, but that's okay
    sendGetFrameSize();
}

void DisplaxTouch::processDisableUsbReporting(uint8_t* data, size_t length) {
    log("Received disable USB reporting response");

    // Consume the response
    consumeBuffer(2);

    // Send next initialization command
    sendEnableReporting();
}

void DisplaxTouch::processEnableUsbReporting(uint8_t* data, size_t length) {
    log("Received enable USB reporting response");

    // Consume the response
    consumeBuffer(2);
}

uint8_t DisplaxTouch::getTouchCount() const {
    return touchCount;
}

const TouchPoint& DisplaxTouch::getTouch(uint8_t index) const {
    return touches[index];
}

uint16_t DisplaxTouch::getScanTime() const {
    return scanTime;
}

bool DisplaxTouch::isTouched() const {
    return touchCount > 0;
}

uint16_t DisplaxTouch::getFrameWidth() const {
    return frameWidth;
}

uint16_t DisplaxTouch::getFrameHeight() const {
    return frameHeight;
}

void DisplaxTouch::setFrameSize(uint16_t width, uint16_t height) {
    frameWidth = width;
    frameHeight = height;
    log("Frame size manually set to %u x %u", frameWidth, frameHeight);
}

void DisplaxTouch::clearTouches() {
    touchCount = 0;
}

int DisplaxTouch::addTouchListener(TouchCallback callback) {
    // Validate callback and check capacity
    if (callback == nullptr || listenerCount >= MAX_LISTENERS) {
        return -1;
    }

    // Assign unique ID and add listener to array
    int listenerId = nextListenerId++;
    listeners[listenerCount] = callback;
    listenerIds[listenerCount] = listenerId;
    listenerCount++;

    return listenerId;
}

bool DisplaxTouch::removeTouchListener(int listenerId) {
    // Search for the listener by ID
    for (uint8_t index = 0; index < listenerCount; index++) {
        if (listenerIds[index] == listenerId) {
            // Shift remaining listeners down to fill the gap
            for (uint8_t shiftIndex = index; shiftIndex < listenerCount - 1; shiftIndex++) {
                listeners[shiftIndex] = listeners[shiftIndex + 1];
                listenerIds[shiftIndex] = listenerIds[shiftIndex + 1];
            }

            listenerCount--;

            return true;
        }
    }

    return false;
}

void DisplaxTouch::setLogCallback(TouchLogCallback callback) {
    logCallback = callback;
}

TouchState DisplaxTouch::getTouchState() const {
    return state;
}

void DisplaxTouch::setStateChangeCallback(StateChangeCallback callback) {
    stateChangeCallback = callback;
}

void DisplaxTouch::log(const char* format, ...) {
    if (!logCallback) {
        return;
    }

    char buffer[LOG_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    logCallback(TouchLogLevel::Info, buffer);
}

void DisplaxTouch::warn(const char* format, ...) {
    if (!logCallback) {
        return;
    }

    char buffer[LOG_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    logCallback(TouchLogLevel::Warn, buffer);
}

inline String DisplaxTouch::getCommandName(Command command) {
    switch (command) {
        case Command::RESET:
            return "RESET";

        case Command::GET_HID_DESCRIPTOR:
            return "GET_HID_DESCRIPTOR";

        case Command::GET_HID_REPORT_DESCRIPTION:
            return "GET_HID_REPORT_DESCRIPTION";

        case Command::GET_FRAME_SIZE:
            return "GET_FRAME_SIZE";

        case Command::TOUCH_REPORT_ID:
            return "TOUCH_REPORT_ID";

        case Command::ENABLE_REPORTING:
            return "ENABLE_REPORTING";

        case Command::DISABLE_REPORTING:
            return "DISABLE_REPORTING";

        case Command::RESET_RESPONSE:
            return "RESET_RESPONSE";

        case Command::DISABLE_USB_REPORTING:
            return "DISABLE_USB_REPORTING";

        case Command::ENABLE_USB_REPORTING:
            return "ENABLE_USB_REPORTING";

        default:
            return "UNKNOWN";
    }
}

inline String DisplaxTouch::getStateName(TouchState state) {
    switch (state) {
        case TouchState::DISCONNECTED:
            return "DISCONNECTED";

        case TouchState::INITIALIZING:
            return "INITIALIZING";

        case TouchState::CONNECTED:
            return "CONNECTED";

        case TouchState::INITIALIZATION_FAILED:
            return "INITIALIZATION_FAILED";

        case TouchState::SYNCHRONIZED:
            return "SYNCHRONIZED";

        case TouchState::SYNCHRONIZING:
            return "SYNCHRONIZING";

        default:
            return "UNKNOWN";
    }
}

inline String DisplaxTouch::idToHex(unsigned long id) {
    char buffer[7];
    snprintf(buffer, sizeof(buffer), "0x%04X", id);

    return String(buffer);
}

inline String DisplaxTouch::bufferToHex(const uint8_t* buffer, uint8_t length, String name) {
    if (length == 0 || buffer == nullptr) {
        return "n/a";
    }

    String result = String("[") + length + String("] ");

    if (name.length() > 0) {
        result += name + String(": ");
    }

    for (uint8_t i = 0; i < length; i++) {
        if (i > 0) {
            result += " ";
        }

        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", buffer[i]);
        result += hex;
    }

    return result;
}