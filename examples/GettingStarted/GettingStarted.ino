#include <DisplaxTouch.hpp>

DisplaxTouch touch(Serial1);

void setup() {
    // Setup serial port
    Serial1.begin(115200);

    // Enable optional logging
    touch.setLogCallback([](TouchLogLevel level, const char* message) {
        if (level == TouchLogLevel::Info) {
            Serial.println(String("INFO: ") + message);
        } else {
            Serial.println(String("WARN: ") + message);
        }
    });

    // Monitor state changes for connection detection
    touch.setStateChangeCallback([](TouchState newState, TouchState previousState) {
        if (newState == TouchState::CONNECTED) {
            Serial.println("Touch sensor connected");
        } else if (newState == TouchState::INITIALIZATION_FAILED) {
            Serial.println("Touch sensor failed to initialize");
        }
    });

    // Log touch events
    touch.addTouchListener([](const TouchPoint* touchPoints, uint8_t count) {
        for (uint8_t i = 0; i < count; i++) {
            const TouchPoint& touchPoint = touchPoints[i];

            float normalizedX = static_cast<float>(touchPoint.x) / static_cast<float>(touchPoint.frameWidth);
            float normalizedY = static_cast<float>(touchPoint.y) / static_cast<float>(touchPoint.frameHeight);

            Serial.print("Touch id: ");
            Serial.print(static_cast<int>(touchPoint.id));
            Serial.print(", x: ");
            Serial.print(normalizedX);
            Serial.print(" y: ");
            Serial.print(normalizedY);
            Serial.print(", width: ");
            Serial.print(static_cast<int>(touchPoint.width));
            Serial.print(", height: ");
            Serial.print(static_cast<int>(touchPoint.height));
            Serial.print(", pressure: ");
            Serial.println(touchPoint.pressure);
        }
    });

    // Initialize the touch sensor
    touch.begin();
}

void loop() {
    // Process touch events
    touch.loop();
}