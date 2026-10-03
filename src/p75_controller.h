#pragma once

#include "p75_protocol.h"

#include <hidapi.h>

#include <cstdint>
#include <string>
#include <vector>

namespace p75 {

struct HidCandidate {
    std::string path;
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
    std::uint16_t usagePage = 0;
    std::uint16_t usage = 0;
    bool controlInterface = false;
};

struct DeviceSnapshot {
    std::uint16_t firmwareVersion = 0;
    std::uint8_t protocolVersion = 0;
    std::uint8_t deviceType = 0;
    std::uint8_t matrixScreenFlag = 0;
    std::uint8_t matrixScreenColumns = 0;
    std::uint8_t matrixScreenRows = 0;
    bool matrixScreenAvailable = false;
    int mainBrightnessMax = 255;
    int mainSpeedMax = 5;
    int frontBrightnessMax = 255;
    int frontSpeedMax = 5;
    int sideBrightnessMax = 255;
    int sideSpeedMax = 5;
    int matrixBrightnessMax = 255;
    int matrixSpeedMax = 5;
    protocol::LightAreaSettings keyboard;
    protocol::LightAreaSettings front;
    protocol::LightAreaSettings sides;
    protocol::MatrixSettings matrix;
    protocol::MatrixPixels pixels{};
    int customLightSlot = 0;
    protocol::UserLightColors keyColors{};
    bool keyColorsAvailable = false;
};

class P75Controller {
public:
    P75Controller() = default;
    ~P75Controller();
    P75Controller(const P75Controller&) = delete;
    P75Controller& operator=(const P75Controller&) = delete;

    static std::vector<HidCandidate> enumerate(std::string& error);

    bool connect(const std::string& path, std::string& error);
    void disconnect();
    bool isConnected() const { return device_ != nullptr; }

    bool readAll(DeviceSnapshot& snapshot, std::string& error);
    bool applyLightArea(int area, const protocol::LightAreaSettings& settings,
                        const DeviceSnapshot& snapshot, std::string& error);
    bool applyKeyboardKeyColors(const protocol::UserLightColors& colors,
                                const DeviceSnapshot& snapshot, std::string& error);
    bool applyMatrixSettings(const protocol::MatrixSettings& settings,
                             const DeviceSnapshot& snapshot, std::string& error);
    bool uploadMatrix(const protocol::MatrixPixels& pixels,
                      std::string& error);
    bool applyMatrixImage(const protocol::MatrixSettings& settings,
                          const protocol::MatrixPixels& pixels,
                          const DeviceSnapshot& snapshot,
                          std::string& error);

private:
    using SessionAction = bool (*)(P75Controller&, void*, std::string&);

    bool sendCommand(std::uint8_t command, std::uint16_t offset,
                     std::uint8_t count, const std::uint8_t* requestPayload,
                     std::vector<std::uint8_t>& responsePayload,
                     std::string& error);
    bool readChunked(std::uint8_t command, std::size_t size,
                     std::vector<std::uint8_t>& bytes, std::string& error);
    bool writeChunked(std::uint8_t command, const std::vector<std::uint8_t>& bytes,
                      std::string& error);
    bool withSession(SessionAction action, void* context, std::string& error);
    bool writeFunctionInfo(const protocol::FunctionInfo& info, std::string& error);

    hid_device* device_ = nullptr;
};

} // namespace p75

