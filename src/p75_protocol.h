#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace p75::protocol {

inline constexpr std::uint16_t kVendorId = 0x36B0;
inline constexpr std::uint16_t kProductId = 0x302B;
inline constexpr std::uint16_t kControlUsagePage = 0xFF60;
inline constexpr std::uint16_t kControlUsage = 0x0061;
inline constexpr std::uint8_t kReportId = 0xAA;
inline constexpr std::uint8_t kSuccess = 0x55;
inline constexpr std::uint8_t kFailure = 0x0F;
inline constexpr std::size_t kPacketSize = 65;
inline constexpr std::size_t kChunkSize = 56;
inline constexpr std::size_t kMatrixPixels = 49;
inline constexpr std::size_t kMatrixBytes = kMatrixPixels * 3;
inline constexpr std::uint8_t kStartComm = 16;
inline constexpr std::uint8_t kStopComm = 17;
inline constexpr std::uint8_t kGetDeviceInfo = 18;
inline constexpr std::uint8_t kGetFuncInfo = 20;
inline constexpr std::uint8_t kSetFuncInfo = 21;
inline constexpr std::uint8_t kGetMatrixCustom = 58;
inline constexpr std::uint8_t kSetMatrixCustom = 59;
inline constexpr std::uint8_t kSetMatrixModePrepare = 61;

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

using MatrixPixels = std::array<Rgb, kMatrixPixels>;
using FunctionInfo = std::array<std::uint8_t, 64>;

struct LightAreaSettings {
    bool enabled = true;
    int mode = 0;
    int brightness = 128;
    int speed = 2;
    Rgb color{180, 100, 255};
};

struct MatrixSettings {
    bool enabled = true;
    int mode = 5; // PMO Hub identifies matrix mode 5 as the custom-image mode.
    int brightness = 128;
    int speed = 2;
    Rgb color{255, 255, 255};
};

std::array<std::uint8_t, kPacketSize> makeRequest(
    std::uint8_t command,
    std::uint16_t offset = 0,
    std::uint8_t count = 0,
    const std::uint8_t* payload = nullptr,
    std::size_t payloadSize = 0);

bool parseResponse(
    const std::uint8_t* bytes,
    std::size_t size,
    std::uint8_t expectedCommand,
    std::uint16_t expectedOffset,
    std::size_t expectedCount,
    std::vector<std::uint8_t>& payload,
    std::string& error);

std::vector<std::uint8_t> encodeMatrix(const MatrixPixels& pixels);
MatrixPixels decodeMatrix(const std::vector<std::uint8_t>& bytes);

void updateLightArea(FunctionInfo& info, const LightAreaSettings& settings,
                     int area, int maxBrightness, int maxSpeed);
void updateMatrixSettings(FunctionInfo& info, const MatrixSettings& settings,
                          int maxBrightness, int maxSpeed);

} // namespace p75::protocol

