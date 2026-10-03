#include "p75_controller.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <thread>

namespace p75 {
namespace {

std::uint16_t readLe16(const std::vector<std::uint8_t>& bytes, std::size_t index) {
    if (index + 1 >= bytes.size()) return 0;
    return static_cast<std::uint16_t>(bytes[index] |
        (static_cast<std::uint16_t>(bytes[index + 1]) << 8));
}

int nonzeroOr(int value, int fallback) {
    return value > 0 ? value : fallback;
}

protocol::LightAreaSettings parseLightArea(const protocol::FunctionInfo& info,
                                           int base, int maxBrightness,
                                           int maxSpeed) {
    protocol::LightAreaSettings result;
    result.enabled = info[base + 1] == 0;
    result.mode = info[base + 2];
    result.brightness = std::min<int>(info[base + 3], maxBrightness);
    result.speed = std::clamp(maxSpeed - static_cast<int>(info[base + 4]), 0, maxSpeed);
    result.color = {info[base + 7], info[base + 8], info[base + 9]};
    return result;
}

protocol::MatrixSettings parseMatrix(const protocol::FunctionInfo& info,
                                     int maxBrightness, int maxSpeed) {
    protocol::MatrixSettings result;
    result.enabled = info[29] == 0;
    result.mode = info[30];
    result.brightness = std::min<int>(info[31], maxBrightness);
    result.speed = std::clamp(maxSpeed - static_cast<int>(info[32]), 0, maxSpeed);
    result.color = {info[35], info[36], info[37]};
    return result;
}

} // namespace

P75Controller::~P75Controller() {
    disconnect();
    hid_exit();
}

std::vector<HidCandidate> P75Controller::enumerate(std::string& error) {
    std::vector<HidCandidate> result;
    if (hid_init() != 0) {
        error = "HIDAPI could not initialize on this system.";
        return result;
    }

    hid_device_info* list = hid_enumerate(protocol::kVendorId, protocol::kProductId);
    for (auto* item = list; item != nullptr; item = item->next) {
        HidCandidate candidate;
        candidate.path = item->path != nullptr ? item->path : "";
        candidate.vendorId = item->vendor_id;
        candidate.productId = item->product_id;
        candidate.usagePage = item->usage_page;
        candidate.usage = item->usage;
        candidate.controlInterface = candidate.usagePage == protocol::kControlUsagePage &&
                                     candidate.usage == protocol::kControlUsage;
        result.push_back(std::move(candidate));
    }
    hid_free_enumeration(list);
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.controlInterface && !b.controlInterface;
    });
    if (result.empty()) {
        error = "No PMO P75 USB control interface found. Connect the keyboard by USB and set it to wired mode.";
    } else {
        error.clear();
    }
    return result;
}

bool P75Controller::connect(const std::string& path, std::string& error) {
    disconnect();
    if (hid_init() != 0) {
        error = "HIDAPI could not initialize on this system.";
        return false;
    }
    device_ = hid_open_path(path.c_str());
    if (device_ == nullptr) {
        error = "Could not open the P75 control interface. Close PMO Hub and reconnect the keyboard by USB.";
        return false;
    }
    if (hid_set_nonblocking(device_, 0) != 0) {
        error = "Could not configure the P75 HID connection.";
        disconnect();
        return false;
    }
    error.clear();
    return true;
}

void P75Controller::disconnect() {
    if (device_ != nullptr) {
        hid_close(device_);
        device_ = nullptr;
    }
}

bool P75Controller::sendCommand(std::uint8_t command, std::uint16_t offset,
                                std::uint8_t count, const std::uint8_t* requestPayload,
                                std::vector<std::uint8_t>& responsePayload,
                                std::string& error) {
    if (device_ == nullptr) {
        error = "P75 is disconnected.";
        return false;
    }
    const auto packet = protocol::makeRequest(command, offset, count,
                                               requestPayload,
                                               requestPayload == nullptr ? 0 : count);
    for (int attempt = 0; attempt < 3; ++attempt) {
        const int written = hid_write(device_, packet.data(), packet.size());
        if (written < 0) {
            error = "HID write failed. Close other keyboard software and reconnect by USB.";
            return false;
        }

        std::array<std::uint8_t, protocol::kPacketSize> input{};
        const int received = hid_read_timeout(device_, input.data(), input.size(), 1200);
        if (received > 0 && protocol::parseResponse(input.data(),
                static_cast<std::size_t>(received), command, offset, count,
                responsePayload, error)) {
            return true;
        }
        if (attempt < 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(35));
        }
    }
    if (error.empty()) error = "Timed out waiting for the P75 response.";
    return false;
}

bool P75Controller::readChunked(std::uint8_t command, std::size_t size,
                                std::vector<std::uint8_t>& bytes,
                                std::string& error) {
    bytes.clear();
    bytes.reserve(size);
    for (std::size_t offset = 0; offset < size; offset += protocol::kChunkSize) {
        const auto count = std::min(protocol::kChunkSize, size - offset);
        std::vector<std::uint8_t> payload;
        if (!sendCommand(command, static_cast<std::uint16_t>(offset),
                         static_cast<std::uint8_t>(count), nullptr, payload, error)) {
            return false;
        }
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }
    return true;
}

bool P75Controller::writeChunked(std::uint8_t command,
                                 const std::vector<std::uint8_t>& bytes,
                                 std::string& error) {
    for (std::size_t offset = 0; offset < bytes.size(); offset += protocol::kChunkSize) {
        const auto count = std::min(protocol::kChunkSize, bytes.size() - offset);
        std::vector<std::uint8_t> response;
        if (!sendCommand(command, static_cast<std::uint16_t>(offset),
                         static_cast<std::uint8_t>(count), bytes.data() + offset,
                         response, error)) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

bool P75Controller::withSession(SessionAction action, void* context,
                                std::string& error) {
    std::vector<std::uint8_t> response;
    if (!sendCommand(protocol::kStartComm, 0, 0, nullptr, response, error)) return false;
    const bool actionOk = action(*this, context, error);
    std::string stopError;
    const bool stopOk = sendCommand(protocol::kStopComm, 0, 0, nullptr,
                                    response, stopError);
    if (!actionOk) return false;
    if (!stopOk) {
        error = "P75 communication ended with an error: " + stopError;
        return false;
    }
    return true;
}

bool P75Controller::writeFunctionInfo(const protocol::FunctionInfo& info,
                                      std::string& error) {
    return writeChunked(protocol::kSetFuncInfo,
                        std::vector<std::uint8_t>(info.begin(), info.end()), error);
}

bool P75Controller::readAll(DeviceSnapshot& snapshot, std::string& error) {
    struct ReadContext { DeviceSnapshot* snapshot; } context{&snapshot};
    auto action = +[](P75Controller& self, void* opaque, std::string& actionError) -> bool {
        auto& out = *static_cast<ReadContext*>(opaque)->snapshot;
        std::vector<std::uint8_t> deviceInfo;
        std::vector<std::uint8_t> funcData;
        if (!self.readChunked(protocol::kGetDeviceInfo, 56, deviceInfo, actionError) ||
            !self.readChunked(protocol::kGetFuncInfo, 64, funcData, actionError)) return false;
        if (deviceInfo.size() < 37 || funcData.size() < 64) {
            actionError = "P75 returned incomplete device settings.";
            return false;
        }
        if (readLe16(deviceInfo, 2) != protocol::kVendorId ||
            readLe16(deviceInfo, 4) != protocol::kProductId) {
            actionError = "The connected HID interface did not identify as a PMO P75.";
            return false;
        }
        out.firmwareVersion = readLe16(deviceInfo, 6);
        out.protocolVersion = deviceInfo[8];
        out.deviceType = deviceInfo[11];
        out.matrixScreenFlag = deviceInfo[31];
        out.matrixScreenColumns = deviceInfo[33];
        out.matrixScreenRows = deviceInfo[34];
        out.mainBrightnessMax = nonzeroOr(deviceInfo[16], 255);
        out.mainSpeedMax = nonzeroOr(deviceInfo[17], 5);
        out.frontBrightnessMax = nonzeroOr(deviceInfo[21], out.mainBrightnessMax);
        out.frontSpeedMax = nonzeroOr(deviceInfo[22], out.mainSpeedMax);
        out.sideBrightnessMax = nonzeroOr(deviceInfo[28], out.mainBrightnessMax);
        out.sideSpeedMax = nonzeroOr(deviceInfo[29], out.mainSpeedMax);
        out.matrixBrightnessMax = nonzeroOr(deviceInfo[35], 255);
        out.matrixSpeedMax = nonzeroOr(deviceInfo[36], 5);
        // The product identity (expected device type 102) comes from its PID.
        // Device-info byte 11 is a different, lower-level keyboard type.
        out.matrixScreenAvailable = out.protocolVersion >= 2 && deviceInfo[31] != 0;

        protocol::FunctionInfo info{};
        std::copy_n(funcData.begin(), info.size(), info.begin());
        out.keyboard = parseLightArea(info, 0, out.mainBrightnessMax, out.mainSpeedMax);
        out.front = parseLightArea(info, 10, out.frontBrightnessMax, out.frontSpeedMax);
        out.sides = parseLightArea(info, 19, out.sideBrightnessMax, out.sideSpeedMax);
        out.matrix = parseMatrix(info, out.matrixBrightnessMax, out.matrixSpeedMax);
        out.customLightSlot = std::min<int>(info[10],
            static_cast<int>(protocol::kGetUserLightCommands.size()) - 1);

        if (out.matrixScreenAvailable) {
            std::vector<std::uint8_t> matrixData;
            if (!self.readChunked(protocol::kGetMatrixCustom, protocol::kMatrixBytes,
                                  matrixData, actionError)) return false;
            out.pixels = protocol::decodeMatrix(matrixData);
        }

        std::vector<std::uint8_t> userLightData;
        const auto command = protocol::kGetUserLightCommands[
            static_cast<std::size_t>(out.customLightSlot)];
        if (self.readChunked(command, protocol::kUserLightBytes,
                             userLightData, actionError)) {
            out.keyColors = protocol::decodeUserLight(userLightData);
            out.keyColorsAvailable = true;
        } else {
            // Keep basic settings usable on firmware that lacks custom-light
            // profile reads. The paint view will start with an empty palette.
            out.keyColors.fill(protocol::Rgb{});
            out.keyColorsAvailable = false;
            actionError.clear();
        }
        return true;
    };
    return withSession(action, &context, error);
}

bool P75Controller::applyKeyboardKeyColors(const protocol::UserLightColors& colors,
                                           const DeviceSnapshot& snapshot,
                                           std::string& error) {
    const int slot = std::clamp(snapshot.customLightSlot, 0,
        static_cast<int>(protocol::kSetUserLightCommands.size()) - 1);
    const auto encoded = protocol::encodeUserLight(colors);
    const std::vector<std::uint8_t> bytes(encoded.begin(), encoded.end());
    struct Context {
        const std::vector<std::uint8_t>* bytes;
        int slot;
    } context{&bytes, slot};
    auto action = +[](P75Controller& self, void* opaque,
                      std::string& actionError) -> bool {
        const auto& args = *static_cast<Context*>(opaque);
        const auto setCommand = protocol::kSetUserLightCommands[
            static_cast<std::size_t>(args.slot)];
        if (!self.writeChunked(setCommand, *args.bytes, actionError)) return false;

        std::vector<std::uint8_t> funcData;
        if (!self.readChunked(protocol::kGetFuncInfo, 64, funcData, actionError)) {
            return false;
        }
        protocol::FunctionInfo info{};
        std::copy_n(funcData.begin(), info.size(), info.begin());
        info[1] = 0; // PMO's lightSwitch value 0 means enabled.
        info[2] = protocol::kCustomKeyLightMode;
        info[10] = static_cast<std::uint8_t>(args.slot);
        return self.writeFunctionInfo(info, actionError);
    };
    return withSession(action, &context, error);
}

bool P75Controller::applyLightArea(int area,
                                   const protocol::LightAreaSettings& settings,
                                   const DeviceSnapshot& snapshot,
                                   std::string& error) {
    struct Context {
        int area;
        protocol::LightAreaSettings settings;
        const DeviceSnapshot* snapshot;
    } context{area, settings, &snapshot};
    auto action = +[](P75Controller& self, void* opaque, std::string& actionError) -> bool {
        const auto& args = *static_cast<Context*>(opaque);
        std::vector<std::uint8_t> bytes;
        if (!self.readChunked(protocol::kGetFuncInfo, 64, bytes, actionError)) return false;
        protocol::FunctionInfo info{};
        std::copy_n(bytes.begin(), info.size(), info.begin());
        const int brightnessMax = args.area == 0 ? args.snapshot->mainBrightnessMax :
                                  args.area == 1 ? args.snapshot->frontBrightnessMax :
                                                   args.snapshot->sideBrightnessMax;
        const int speedMax = args.area == 0 ? args.snapshot->mainSpeedMax :
                             args.area == 1 ? args.snapshot->frontSpeedMax :
                                              args.snapshot->sideSpeedMax;
        protocol::updateLightArea(info, args.settings, args.area, brightnessMax, speedMax);
        return self.writeFunctionInfo(info, actionError);
    };
    return withSession(action, &context, error);
}

bool P75Controller::applyMatrixSettings(const protocol::MatrixSettings& settings,
                                        const DeviceSnapshot& snapshot,
                                        std::string& error) {
    struct Context { protocol::MatrixSettings settings; const DeviceSnapshot* snapshot; };
    Context context{settings, &snapshot};
    auto action = +[](P75Controller& self, void* opaque, std::string& actionError) -> bool {
        const auto& args = *static_cast<Context*>(opaque);
        std::vector<std::uint8_t> response;
        if (!self.sendCommand(protocol::kSetMatrixModePrepare, 0,
                              static_cast<std::uint8_t>(protocol::kChunkSize),
                              nullptr, response, actionError)) return false;
        std::vector<std::uint8_t> bytes;
        if (!self.readChunked(protocol::kGetFuncInfo, 64, bytes, actionError)) return false;
        protocol::FunctionInfo info{};
        std::copy_n(bytes.begin(), info.size(), info.begin());
        protocol::updateMatrixSettings(info, args.settings,
                                       args.snapshot->matrixBrightnessMax,
                                       args.snapshot->matrixSpeedMax);
        return self.writeFunctionInfo(info, actionError);
    };
    return withSession(action, &context, error);
}

bool P75Controller::uploadMatrix(const protocol::MatrixPixels& pixels,
                                 std::string& error) {
    const auto bytes = protocol::encodeMatrix(pixels);
    struct Context { const std::vector<std::uint8_t>* bytes; } context{&bytes};
    auto action = +[](P75Controller& self, void* opaque, std::string& actionError) -> bool {
        const auto& data = *static_cast<Context*>(opaque)->bytes;
        return self.writeChunked(protocol::kSetMatrixCustom, data, actionError);
    };
    return withSession(action, &context, error);
}

bool P75Controller::applyMatrixImage(const protocol::MatrixSettings& settings,
                                     const protocol::MatrixPixels& pixels,
                                     const DeviceSnapshot& snapshot,
                                     std::string& error) {
    // Match PMO Hub's sequence: write the display mode in one communication
    // session, then upload the custom matrix bytes in a fresh session.
    return applyMatrixSettings(settings, snapshot, error) && uploadMatrix(pixels, error);
}

} // namespace p75

