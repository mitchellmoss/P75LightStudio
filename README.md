# P75 Light Studio

A small local C++ desktop UI for PMO P75 lighting and its 7 × 7 matrix screen. It uses the P75 HID interface directly, so it does not need PMO's Windows application or a browser session.

## What it can change

- Keyboard keys, front strip, and side lighting: enabled state, numeric effect mode, brightness, speed, and color.
- Matrix screen: enabled state, numeric display mode, brightness, speed, and color.
- A hand-painted 7 × 7 RGB image stored in the keyboard's custom matrix slot.

Mode names are shown as the keyboard's numeric values because PMO's effect list is firmware-dependent. The app reads the current mode first and preserves hidden color/mix fields when applying edits. PMO Hub identifies matrix mode `5` as custom image mode. The current screen editor sends still images; it does not upload GIFs or animations.

## Connect

1. Connect the keyboard with USB and set it to wired mode.
2. Close PMO Hub and any other program currently controlling the keyboard.
3. Launch `P75LightStudio.exe`, click **Refresh**, select the **P75 control interface**, then click **Connect**.
4. Edit a lighting area or paint the matrix, then use its **Apply** button.

The app only writes settings commands. It does not update firmware, remap keys, or reset the keyboard. If the device reports an unsupported HID interface or firmware, the app leaves controls disconnected rather than sending guessed commands.

## Build and development loop

The project uses CMake, C++20, SDL3, Dear ImGui, and HIDAPI. CMake downloads the pinned SDL3, Dear ImGui, and HIDAPI sources on the first configure; no dependency installer is needed. CMake's configure/build flow and HIDAPI's supported CMake build are documented by their maintainers ([CMake tutorial](https://cmake.org/cmake/help/latest/guide/tutorial/Before%20You%20Begin.html), [HIDAPI CMake build guide](https://github.com/libusb/hidapi/blob/master/BUILD.cmake.md)).

On Windows, macOS, or Linux with CMake, Git, and a C++20 compiler installed:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Use the same loop after each change:

```sh
# Protocol framing and conversion checks; does not require the keyboard.
./build/bin/P75LightStudio --self-test

# Print detected P75 USB interfaces and their control usage.
./build/bin/P75LightStudio --list-devices

# Read the connected P75's current settings without changing them.
./build/bin/P75LightStudio --read-device

# Opens the actual SDL + Dear ImGui window, renders several frames, and exits.
./build/bin/P75LightStudio --smoke-ui

# Opens the UI in preview mode without writing to hardware.
./build/bin/P75LightStudio --demo
```

On Windows, the executable is `build/bin/Release/P75LightStudio.exe` or `build/bin/P75LightStudio.exe`, depending on the generator. The same CMake source builds against each platform's SDL and HIDAPI backends; the protocol and UI logic stay platform-independent.

## Protocol notes

The implementation follows PMO Hub's public browser driver: P75 USB vendor/product IDs `0x36B0:0x302B`, custom HID usage `0xFF60:0x0061`, 65-byte HIDAPI output packets, `0xAA` command header, 56-byte data chunks, and the PMO success marker `0x55`. Matrix custom data is 49 RGB pixels (147 bytes), and the screen-settings fields use the v2 function-info layout.

This implementation was derived from the client code served by [PMO Hub](https://pmohub.cn/), with P75 identity also listed in PMO's [official download center](https://pmolab.cn/col.jsp?id=113). The general HID report model is described in [Chrome's WebHID documentation](https://developer.chrome.com/docs/capabilities/hid/).

## First-run validation

The checked-in protocol self-test covers report framing, response parsing, P75 mode fields, and 7 × 7 RGB packing. The UI smoke run verifies the SDL event/render loop. A physical P75 is required to confirm writes on the exact keyboard firmware; the device's read-back values are used to preserve its other settings when changing one area.

