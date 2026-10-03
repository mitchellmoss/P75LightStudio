#include "p75_protocol.h"

#include <algorithm>

namespace p75::protocol {

std::array<std::uint8_t, kPacketSize> makeRequest(
    std::uint8_t command, std::uint16_t offset, std::uint8_t count,
    const std::uint8_t* payload, std::size_t payloadSize) {
    std::array<std::uint8_t, kPacketSize> packet{};
    packet[0] = 0; // HIDAPI's report-ID byte; PMO Hub sends report ID 0.
    packet[1] = kReportId;
    packet[2] = command;
    packet[3] = static_cast<std::uint8_t>(offset & 0xFF);
    packet[4] = static_cast<std::uint8_t>((offset >> 8) & 0xFF);
    packet[5] = count;

    const auto n = std::min({payloadSize, static_cast<std::size_t>(count), kChunkSize});
    if (payload != nullptr) {
        for (std::size_t i = 0; i < n; ++i) {
            packet[9 + i] = payload[i];
        }
    }
    return packet;
}

bool parseResponse(const std::uint8_t* bytes, std::size_t size,
                   std::uint8_t expectedCommand, std::uint16_t expectedOffset,
                   std::size_t expectedCount, std::vector<std::uint8_t>& payload,
                   std::string& error) {
    payload.clear();
    // Some HIDAPI backends include a leading zero report-ID byte; PMO Hub's
    // WebHID wrapper omits that byte for this unnumbered report.
    if (size >= 2 && bytes[0] == 0 && bytes[1] == kReportId) {
        ++bytes;
        --size;
    }
    if (size < 8) {
        error = "P75 response is shorter than its 8-byte header";
        return false;
    }
    if (bytes[0] != kReportId || bytes[1] != expectedCommand ||
        bytes[2] != static_cast<std::uint8_t>(expectedOffset & 0xFF) ||
        bytes[3] != static_cast<std::uint8_t>((expectedOffset >> 8) & 0xFF)) {
        error = "P75 response header does not match the command";
        return false;
    }
    if (bytes[7] != kSuccess) {
        error = bytes[7] == kFailure
            ? "P75 rejected the command (status 0x0F)"
            : "P75 returned an unknown command status";
        return false;
    }
    if (size < 8 + expectedCount) {
        error = "P75 response does not contain the requested data";
        return false;
    }
    payload.assign(bytes + 8, bytes + 8 + expectedCount);
    return true;
}

std::vector<std::uint8_t> encodeMatrix(const MatrixPixels& pixels) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kMatrixBytes);
    for (const auto& pixel : pixels) {
        bytes.push_back(pixel.r);
        bytes.push_back(pixel.g);
        bytes.push_back(pixel.b);
    }
    return bytes;
}

MatrixPixels decodeMatrix(const std::vector<std::uint8_t>& bytes) {
    MatrixPixels pixels{};
    for (std::size_t i = 0; i < kMatrixPixels; ++i) {
        const auto base = i * 3;
        if (base + 2 < bytes.size()) {
            pixels[i] = Rgb{bytes[base], bytes[base + 1], bytes[base + 2]};
        }
    }
    return pixels;
}

std::array<std::uint8_t, kUserLightBytes> encodeUserLight(const UserLightColors& colors) {
    std::array<std::uint8_t, kUserLightBytes> bytes{};
    for (std::size_t index = 0; index < colors.size(); ++index) {
        bytes[index * 3] = colors[index].r;
        bytes[index * 3 + 1] = colors[index].g;
        bytes[index * 3 + 2] = colors[index].b;
    }
    return bytes;
}

UserLightColors decodeUserLight(const std::vector<std::uint8_t>& bytes) {
    UserLightColors colors{};
    const auto size = std::min(bytes.size(), kUserLightBytes);
    for (std::size_t offset = 0; offset + 2 < size; offset += 3) {
        colors[offset / 3] = Rgb{bytes[offset], bytes[offset + 1], bytes[offset + 2]};
    }
    return colors;
}

void updateLightArea(FunctionInfo& info, const LightAreaSettings& settings,
                     int area, int maxBrightness, int maxSpeed) {
    // PMO Hub's P75 map: keys 1..10, front/decorative strip 11..19,
    // side strips 20..28. A zero switch value means enabled.
    const int base = area == 0 ? 0 : area == 1 ? 10 : 19;
    info[base + 1] = settings.enabled ? 0 : 1;
    info[base + 2] = static_cast<std::uint8_t>(std::clamp(settings.mode, 0, 255));
    info[base + 3] = static_cast<std::uint8_t>(std::clamp(settings.brightness, 0, maxBrightness));
    info[base + 4] = static_cast<std::uint8_t>(std::max(0, maxSpeed - std::clamp(settings.speed, 0, maxSpeed)));
    info[base + 7] = settings.color.r;
    info[base + 8] = settings.color.g;
    info[base + 9] = settings.color.b;
}

void updateMatrixSettings(FunctionInfo& info, const MatrixSettings& settings,
                          int maxBrightness, int maxSpeed) {
    // Protocol v2 matrix fields occupy bytes 29..37.
    info[29] = settings.enabled ? 0 : 1;
    info[30] = static_cast<std::uint8_t>(std::clamp(settings.mode, 0, 255));
    info[31] = static_cast<std::uint8_t>(std::clamp(settings.brightness, 0, maxBrightness));
    info[32] = static_cast<std::uint8_t>(std::max(0, maxSpeed - std::clamp(settings.speed, 0, maxSpeed)));
    info[35] = settings.color.r;
    info[36] = settings.color.g;
    info[37] = settings.color.b;
}

} // namespace p75::protocol

