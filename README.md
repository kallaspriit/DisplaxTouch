# DisplaxTouch

[![PlatformIO Registry](https://badges.registry.platformio.org/packages/kallaspriit/library/DisplaxTouch.svg)](https://registry.platformio.org/libraries/kallaspriit/DisplaxTouch)
![License](https://img.shields.io/github/license/kallaspriit/DisplaxTouch)
![Release](https://img.shields.io/github/v/release/kallaspriit/DisplaxTouch)

Arduino / PlatformIO library for interfacing with **Displax touch controllers** over UART using the Arduino **Stream** API.

- UART: **115200 baud, 8N1** (configure your serial port in your application).
- Portable: works with `HardwareSerial`, RP2040 PIO UART wrappers, USB CDC serials, and many SoftwareSerial-style ports (as long as they implement `Stream`).

## Features

- Works with any `Stream`-compatible serial transport.
- Simple event callbacks for touch data, state changes, and logging.
- Normalized coordinates are easy to compute from the reported frame size.
- Arduino and PlatformIO friendly.

## Requirements

- A Displax touch controller configured for UART output (tested with Displax M64 controller).
- One UART (or `Stream`-compatible) port at **115200 baud, 8N1**.

## Wiring guide

Example pinout guide for Raspberry Pi Pico but similar approach with different pin numbers should work on other microcontrollers.

On the Arduino-Pico core, `Serial1` maps to `UART0` on `GP0/GP1`. Any `Stream`-compatible UART will work.

Displax controller `RX` should be connected to MCU `TX` and vice versa.

| Controller | MCU (Pico example)                    |
| ---------- | ------------------------------------- |
| `5V`       | `VBUS` (USB 5V) or external 5V supply |
| `GND`      | Any `GND` (common ground required)    |
| `RX`       | `GP0` (`UART0 TX`)                    |
| `TX`       | `GP1` (`UART0 RX`)                    |

## Quick start

```cpp
#include <DisplaxTouch.hpp>

DisplaxTouch touch(Serial1);

void setup() {
  Serial1.begin(115200);

  touch.setStateChangeCallback([](TouchState newState, TouchState previousState) {
    if (newState == TouchState::CONNECTED) {
      Serial.println("Touch sensor connected");
    }
  });

  touch.addTouchListener([](const TouchPoint* points, uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
      const TouchPoint& p = points[i];
      float x = static_cast<float>(p.x) / static_cast<float>(p.frameWidth);
      float y = static_cast<float>(p.y) / static_cast<float>(p.frameHeight);
      Serial.print("id=");
      Serial.print(static_cast<int>(p.id));
      Serial.print(" x=");
      Serial.print(x);
      Serial.print(" y=");
      Serial.println(y);
    }
  });

  touch.begin();
}

void loop() {
  touch.loop();
}
```

## Example

See `examples/GettingStarted/GettingStarted.ino` for a fuller example including logging and more detailed output.

Example output:

```
Touch id: 0, x: 0.57 y: 0.51, width: 63, height: 63, pressure: 252
Touch id: 2, x: 0.47 y: 0.46, width: 74, height: 74, pressure: 296
Touch id: 1, x: 0.39 y: 0.51, width: 56, height: 56, pressure: 225
Touch id: 0, x: 0.57 y: 0.51, width: 63, height: 63, pressure: 254
...
```

## Notes

- Call `touch.loop()` regularly to process incoming data.
- Use `setLogCallback` to capture info/warn messages if you want visibility into protocol events.

## Installation

### Arduino IDE

1. Download this repository as a ZIP.
2. Arduino IDE → **Sketch → Include Library → Add .ZIP Library...**
3. Select the downloaded ZIP.

### PlatformIO

Add to `platformio.ini`:

```ini
lib_deps =
  kallaspriit/DisplaxTouch
```
