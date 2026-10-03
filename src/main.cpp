#include "p75_controller.h"
#include "image_import.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

using p75::DeviceSnapshot;
using p75::HidCandidate;
using p75::P75Controller;
using p75::protocol::LightAreaSettings;
using p75::protocol::MatrixPixels;
using p75::protocol::MatrixSettings;
using p75::protocol::Rgb;

struct ImageDialogResult {
    std::mutex mutex;
    bool ready = false;
    std::string path;
    std::string error;
};

constexpr SDL_DialogFileFilter kImageFileFilters[] = {
    {"JPG and PNG images", "jpg;jpeg;png"}
};

void SDLCALL imageFileDialogCallback(void* userdata, const char* const* filelist, int) {
    std::unique_ptr<std::shared_ptr<ImageDialogResult>> holder(
        static_cast<std::shared_ptr<ImageDialogResult>*>(userdata));
    const auto result = *holder;
    std::lock_guard<std::mutex> lock(result->mutex);
    if (filelist == nullptr) {
        const char* message = SDL_GetError();
        result->error = message != nullptr ? message : "The file dialog failed.";
    } else if (filelist[0] != nullptr) {
        result->path = filelist[0];
    }
    result->ready = true;
}

struct AppState {
    P75Controller controller;
    std::vector<HidCandidate> devices;
    int selectedDevice = -1;
    int selectedArea = 0;
    bool demo = false;
    bool loaded = false;
    bool open = true;
    std::string status = "Connect the P75 by USB, then choose its control interface.";
    DeviceSnapshot snapshot;
    LightAreaSettings areaEditor;
    MatrixSettings matrixEditor;
    Rgb brush{255, 112, 196};
    int imageFitMode = 0;
    std::string imageSourceName;
    std::shared_ptr<ImageDialogResult> imageDialogResult;
    bool smoke = false;
    std::chrono::steady_clock::time_point smokeStart;

    void openImageDialog(SDL_Window* window) {
        if (imageDialogResult) return;
        auto result = std::make_shared<ImageDialogResult>();
        imageDialogResult = result;
        auto* callbackData = new std::shared_ptr<ImageDialogResult>(result);
        SDL_ShowOpenFileDialog(imageFileDialogCallback, callbackData, window,
                               kImageFileFilters, 1, nullptr, false);
        status = "Choose a JPG or PNG image.";
    }

    void consumeImageDialogResult() {
        const auto result = imageDialogResult;
        if (!result) return;

        std::string path;
        std::string error;
        {
            std::lock_guard<std::mutex> lock(result->mutex);
            if (!result->ready) return;
            path = result->path;
            error = result->error;
        }
        imageDialogResult.reset();

        if (!error.empty()) {
            status = "Image picker failed: " + error;
            return;
        }
        if (path.empty()) return;

        const auto fit = imageFitMode == 0
            ? p75::image::FitMode::CropToFill
            : p75::image::FitMode::FitWithBlackBars;
        MatrixPixels converted{};
        if (!p75::image::loadImageToMatrix(path, fit, converted, error)) {
            status = error;
            return;
        }

        snapshot.pixels = converted;
        matrixEditor.enabled = true;
        matrixEditor.mode = 5;
        const auto separator = path.find_last_of("/\\");
        imageSourceName = separator == std::string::npos ? path : path.substr(separator + 1);
        status = "Loaded " + imageSourceName + ". Review the 7×7 preview, connect, then upload the custom image.";
    }

    void refresh() {
        std::string error;
        devices = P75Controller::enumerate(error);
        selectedDevice = devices.empty() ? -1 : 0;
        status = error.empty()
            ? "P75 control interface found."
            : error;
    }

    void editAreaFromSnapshot() {
        switch (selectedArea) {
        case 1: areaEditor = snapshot.front; break;
        case 2: areaEditor = snapshot.sides; break;
        default: areaEditor = snapshot.keyboard; break;
        }
    }

    void readFromKeyboard() {
        std::string error;
        if (demo) {
            loaded = true;
            status = "Preview mode: settings are simulated; no keyboard was written.";
            editAreaFromSnapshot();
            return;
        }
        if (!controller.readAll(snapshot, error)) {
            loaded = false;
            status = error;
            return;
        }
        loaded = true;
        matrixEditor = snapshot.matrix;
        editAreaFromSnapshot();
        status = "Read settings from the P75.";
    }

    void connectSelected() {
        if (selectedDevice < 0 || selectedDevice >= static_cast<int>(devices.size())) return;
        const auto& candidate = devices[static_cast<std::size_t>(selectedDevice)];
        if (!candidate.controlInterface) {
            status = "This is a P75 HID interface, but not PMO's vendor control interface. Select the 0xFF60 / 0x0061 interface.";
            return;
        }
        std::string error;
        if (!controller.connect(candidate.path, error)) {
            status = error;
            return;
        }
        status = "Connected. Reading P75 settings…";
        readFromKeyboard();
    }

    void applyArea() {
        if (!loaded) {
            status = "Read the P75 settings before applying changes.";
            return;
        }
        if (demo) {
            if (selectedArea == 1) snapshot.front = areaEditor;
            else if (selectedArea == 2) snapshot.sides = areaEditor;
            else snapshot.keyboard = areaEditor;
            status = "Preview mode: lighting values changed locally; no keyboard was written.";
            return;
        }
        std::string error;
        if (!controller.applyLightArea(selectedArea, areaEditor, snapshot, error)) {
            status = error;
            return;
        }
        if (selectedArea == 1) snapshot.front = areaEditor;
        else if (selectedArea == 2) snapshot.sides = areaEditor;
        else snapshot.keyboard = areaEditor;
        status = "Applied lighting settings to the P75.";
    }

    void applyMatrix(bool withImage) {
        if (!loaded) {
            status = "Read the P75 settings before applying changes.";
            return;
        }
        if (snapshot.matrixScreenAvailable == false && !demo) {
            status = "This firmware did not report the P75 matrix screen interface.";
            return;
        }
        if (withImage) {
            matrixEditor.enabled = true;
            matrixEditor.mode = 5;
        }
        std::string error;
        if (demo) {
            snapshot.matrix = matrixEditor;
            status = "Preview mode: screen values changed locally; no keyboard was written.";
            return;
        }
        const bool success = withImage
            ? controller.applyMatrixImage(matrixEditor, snapshot.pixels, snapshot, error)
            : controller.applyMatrixSettings(matrixEditor, snapshot, error);
        if (!success) {
            status = error;
            return;
        }
        snapshot.matrix = matrixEditor;
        if (withImage) status = "Custom 7×7 image and display settings sent to the P75.";
        else status = "Matrix display settings applied to the P75.";
    }
};

ImVec4 toImVec4(const Rgb& color) {
    return ImVec4(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, 1.0f);
}

Rgb fromFloats(const float color[3]) {
    return Rgb{
        static_cast<std::uint8_t>(std::clamp(std::lround(color[0] * 255.0f), 0L, 255L)),
        static_cast<std::uint8_t>(std::clamp(std::lround(color[1] * 255.0f), 0L, 255L)),
        static_cast<std::uint8_t>(std::clamp(std::lround(color[2] * 255.0f), 0L, 255L))
    };
}

void configureStyle() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.WindowPadding = ImVec2(20.0f, 16.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    auto* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.063f, 0.082f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.085f, 0.094f, 0.120f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.13f, 0.17f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.16f, 0.24f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.18f, 0.31f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.35f, 0.23f, 0.58f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.31f, 0.72f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.54f, 0.37f, 0.83f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.30f, 0.21f, 0.48f, 1.0f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.77f, 0.56f, 1.0f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.72f, 0.51f, 0.97f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.85f, 0.70f, 1.0f, 1.0f);
}

void drawConnectionPanel(AppState& app) {
    ImGui::BeginChild("connection", ImVec2(0, 132), true);
    ImGui::TextUnformatted("KEYBOARD CONNECTION");
    ImGui::Spacing();
    const char* preview = "No P75 control interface found";
    if (app.selectedDevice >= 0 && app.selectedDevice < static_cast<int>(app.devices.size())) {
        const auto& device = app.devices[static_cast<std::size_t>(app.selectedDevice)];
        preview = device.controlInterface ? "PMO P75 · USB control interface" :
                                            "P75 HID interface · not the control channel";
    }
    if (ImGui::BeginCombo("##p75devices", preview, ImGuiComboFlags_WidthFitPreview)) {
        for (int i = 0; i < static_cast<int>(app.devices.size()); ++i) {
            const auto& device = app.devices[static_cast<std::size_t>(i)];
            const bool selected = i == app.selectedDevice;
            std::string label = device.controlInterface
                ? "P75 control interface (0xFF60 / 0x0061)"
                : "Other P75 interface (not selected for writes)";
            label += "##" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), selected)) app.selectedDevice = i;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) app.refresh();
    ImGui::SameLine();
    if (app.controller.isConnected()) {
        if (ImGui::Button("Disconnect")) {
            app.controller.disconnect();
            app.loaded = false;
            app.status = "Disconnected.";
        }
    } else if (app.demo) {
        ImGui::BeginDisabled();
        ImGui::Button("Preview only");
        ImGui::EndDisabled();
    } else if (ImGui::Button("Connect")) {
        app.connectSelected();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!app.controller.isConnected());
    if (ImGui::Button("Read settings")) app.readFromKeyboard();
    ImGui::EndDisabled();

    ImGui::TextWrapped("%s", app.status.c_str());
    ImGui::EndChild();
}

void drawAreaTab(AppState& app) {
    const char* areas[] = {"Keyboard keys", "Front strip", "Side lighting"};
    int area = app.selectedArea;
    if (ImGui::Combo("Lighting area", &area, areas, 3)) {
        app.selectedArea = area;
        app.editAreaFromSnapshot();
    }
    ImGui::Separator();
    ImGui::Checkbox("Lighting enabled", &app.areaEditor.enabled);
    ImGui::InputInt("Effect ID", &app.areaEditor.mode);
    app.areaEditor.mode = std::clamp(app.areaEditor.mode, 0, 255);
    const int brightnessMax = app.selectedArea == 0 ? app.snapshot.mainBrightnessMax :
                              app.selectedArea == 1 ? app.snapshot.frontBrightnessMax :
                                                      app.snapshot.sideBrightnessMax;
    const int speedMax = app.selectedArea == 0 ? app.snapshot.mainSpeedMax :
                         app.selectedArea == 1 ? app.snapshot.frontSpeedMax :
                                                 app.snapshot.sideSpeedMax;
    ImGui::SliderInt("Brightness", &app.areaEditor.brightness, 0, std::max(1, brightnessMax));
    ImGui::SliderInt("Speed", &app.areaEditor.speed, 0, std::max(1, speedMax));
    float color[3] = {
        app.areaEditor.color.r / 255.0f,
        app.areaEditor.color.g / 255.0f,
        app.areaEditor.color.b / 255.0f
    };
    if (ImGui::ColorEdit3("Color", color, ImGuiColorEditFlags_DisplayRGB |
                          ImGuiColorEditFlags_NoInputs)) {
        app.areaEditor.color = fromFloats(color);
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(!app.loaded || (!app.demo && !app.controller.isConnected()));
    if (ImGui::Button("Apply lighting", ImVec2(180, 40))) app.applyArea();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Effect ID is PMO's numeric mode value.");
}

void paintPixel(AppState& app, int index) {
    app.snapshot.pixels[static_cast<std::size_t>(index)] = app.brush;
}

void drawMatrixTab(AppState& app, SDL_Window* window) {
    ImGui::BeginChild("matrix-editor-scroll", ImVec2(0, 0), false);
    ImGui::TextWrapped("Edit the P75's 7 × 7 matrix, or import a JPG/PNG and preview its conversion before uploading.");
    ImGui::Spacing();
    ImGui::Checkbox("Display enabled", &app.matrixEditor.enabled);
    ImGui::InputInt("Display mode", &app.matrixEditor.mode);
    app.matrixEditor.mode = std::clamp(app.matrixEditor.mode, 0, 255);
    ImGui::SameLine();
    ImGui::TextDisabled("Mode 5 is custom image.");
    ImGui::SliderInt("Display brightness", &app.matrixEditor.brightness,
                     0, std::max(1, app.snapshot.matrixBrightnessMax));
    ImGui::SliderInt("Display speed", &app.matrixEditor.speed,
                     0, std::max(1, app.snapshot.matrixSpeedMax));

    float brush[3] = {app.brush.r / 255.0f, app.brush.g / 255.0f, app.brush.b / 255.0f};
    if (ImGui::ColorEdit3("Brush color", brush, ImGuiColorEditFlags_NoInputs)) {
        app.brush = fromFloats(brush);
    }
    ImGui::SameLine();
    if (ImGui::Button("Fill grid")) app.snapshot.pixels.fill(app.brush);
    ImGui::SameLine();
    if (ImGui::Button("Clear grid")) app.snapshot.pixels.fill(Rgb{0, 0, 0});

    ImGui::Separator();
    ImGui::TextUnformatted("IMAGE IMPORT");
    ImGui::BeginDisabled(app.imageDialogResult != nullptr);
    if (ImGui::Button("Load JPG / PNG", ImVec2(170, 38))) app.openImageDialog(window);
    ImGui::EndDisabled();
    ImGui::SameLine();
    const char* fitOptions[] = {"Crop to fill", "Fit with black bars"};
    ImGui::SetNextItemWidth(210.0f);
    ImGui::Combo("Image fit", &app.imageFitMode, fitOptions, 2);
    ImGui::TextDisabled("Fit applies when imported. Uses area averaging in linear RGB; transparent pixels become black.");
    if (!app.imageSourceName.empty()) {
        ImGui::TextWrapped("Preview: %s", app.imageSourceName.c_str());
    }

    ImGui::Spacing();
    const float cellSize = std::clamp(ImGui::GetContentRegionAvail().x / 9.5f, 28.0f, 52.0f);
    ImGui::BeginChild("matrix-canvas", ImVec2(cellSize * 8.5f, cellSize * 8.15f), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 7; ++col) {
            const int index = row * 7 + col;
            ImGui::PushID(index);
            const auto color = toImVec4(app.snapshot.pixels[static_cast<std::size_t>(index)]);
            const bool clicked = ImGui::ColorButton("##pixel", color,
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                ImVec2(cellSize, cellSize));
            if (clicked || (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
                paintPixel(app, index);
            }
            ImGui::PopID();
            if (col < 6) ImGui::SameLine(0.0f, 4.0f);
        }
    }
    ImGui::EndChild();
    ImGui::Spacing();
    const bool unavailable = !app.loaded ||
        (!app.demo && (!app.controller.isConnected() || !app.snapshot.matrixScreenAvailable));
    ImGui::BeginDisabled(unavailable);
    if (ImGui::Button("Apply display settings", ImVec2(205, 40))) app.applyMatrix(false);
    ImGui::SameLine();
    if (ImGui::Button("Upload custom image", ImVec2(205, 40))) app.applyMatrix(true);
    ImGui::EndDisabled();
    ImGui::EndChild();
}

void drawWindow(AppState& app, SDL_Window* window) {
    const auto viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("P75 Light Studio", &app.open, flags);
    ImGui::TextColored(ImVec4(0.84f, 0.72f, 1.0f, 1.0f), "P75 LIGHT STUDIO");
    ImGui::SameLine();
    ImGui::TextDisabled("LOCAL CONTROLS · USB HID");
    ImGui::Separator();
    drawConnectionPanel(app);

    if (app.loaded && !app.demo) {
        ImGui::TextDisabled("P75 · FW 0x%04X · HID protocol %u · %s",
            app.snapshot.firmwareVersion, app.snapshot.protocolVersion,
            app.snapshot.matrixScreenAvailable ? "matrix screen detected" : "lighting ready");
    } else if (app.demo) {
        ImGui::TextColored(ImVec4(0.92f, 0.74f, 0.37f, 1.0f),
                           "PREVIEW MODE · changes stay on this computer");
    }

    if (ImGui::BeginTabBar("P75 controls")) {
        if (ImGui::BeginTabItem("Keyboard lighting")) {
            ImGui::Spacing();
            drawAreaTab(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Matrix screen")) {
            ImGui::Spacing();
            drawMatrixTab(app, window);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - 42.0f));
    ImGui::Separator();
    ImGui::TextDisabled("P75 only · wired USB · no firmware update or key remapping");
    ImGui::End();
}

bool runSelfTest() {
    using namespace p75::protocol;
    const std::array<std::uint8_t, 3> bytes{0x12, 0x34, 0x56};
    const auto request = makeRequest(kSetMatrixCustom, 56, 3, bytes.data(), bytes.size());
    if (request[0] != 0 || request[1] != kReportId || request[2] != kSetMatrixCustom ||
        request[3] != 56 || request[4] != 0 || request[5] != 3 ||
        request[9] != 0x12 || request[10] != 0x34 || request[11] != 0x56) {
        std::fprintf(stderr, "self-test failed: request frame\n");
        return false;
    }
    std::array<std::uint8_t, 64> ack{};
    ack[0] = kReportId;
    ack[1] = kSetMatrixCustom;
    ack[2] = 56;
    ack[7] = kSuccess;
    ack[8] = 0x12;
    ack[9] = 0x34;
    ack[10] = 0x56;
    std::vector<std::uint8_t> response;
    std::string error;
    if (!parseResponse(ack.data(), ack.size(), kSetMatrixCustom, 56, 3, response, error) ||
        response != std::vector<std::uint8_t>(bytes.begin(), bytes.end())) {
        std::fprintf(stderr, "self-test failed: response parser (%s)\n", error.c_str());
        return false;
    }
    MatrixPixels pixels{};
    pixels[0] = Rgb{0x11, 0x22, 0x33};
    const auto matrixBytes = encodeMatrix(pixels);
    if (matrixBytes.size() != kMatrixBytes || matrixBytes[0] != 0x11 ||
        matrixBytes[1] != 0x22 || matrixBytes[2] != 0x33) {
        std::fprintf(stderr, "self-test failed: 7x7 RGB encoding\n");
        return false;
    }
    const std::array<std::uint8_t, 8> twoColorImage{
        255, 0, 0, 255, 0, 0, 255, 255
    };
    MatrixPixels croppedImage{};
    if (!p75::image::convertRgbaToMatrix(twoColorImage.data(), 2, 1,
            p75::image::FitMode::CropToFill, croppedImage, error) ||
        croppedImage[21].r != 255 || croppedImage[21].b != 0 ||
        croppedImage[27].r != 0 || croppedImage[27].b != 255 ||
        std::abs(static_cast<int>(croppedImage[24].r) - 188) > 1 ||
        croppedImage[24].g != 0 ||
        std::abs(static_cast<int>(croppedImage[24].b) - 188) > 1) {
        std::fprintf(stderr, "self-test failed: center-crop image conversion (%s)\n", error.c_str());
        return false;
    }
    MatrixPixels fittedImage{};
    if (!p75::image::convertRgbaToMatrix(twoColorImage.data(), 2, 1,
            p75::image::FitMode::FitWithBlackBars, fittedImage, error) ||
        fittedImage[0].r != 0 || fittedImage[6].r != 0 || fittedImage[42].r != 0 ||
        fittedImage[48].r != 0 || fittedImage[24].r == 0) {
        std::fprintf(stderr, "self-test failed: contain image conversion (%s)\n", error.c_str());
        return false;
    }
    FunctionInfo info{};
    MatrixSettings settings;
    info[33] = 9;
    info[34] = 12;
    updateMatrixSettings(info, settings, 255, 5);
    if (info[29] != 0 || info[30] != 5 || info[31] != 128 || info[32] != 3 ||
        info[33] != 9 || info[34] != 12) {
        std::fprintf(stderr, "self-test failed: matrix settings mapping\n");
        return false;
    }
    FunctionInfo lightInfo{};
    lightInfo[5] = 7;
    lightInfo[6] = 11;
    LightAreaSettings light;
    light.mode = 253;
    updateLightArea(lightInfo, light, 0, 255, 5);
    if (lightInfo[1] != 0 || lightInfo[2] != 253 || lightInfo[5] != 7 ||
        lightInfo[6] != 11) {
        std::fprintf(stderr, "self-test failed: lighting settings mapping\n");
        return false;
    }
    std::puts("Protocol and image-import self-test passed.");
    return true;
}

int listDevices() {
    P75Controller cleanup;
    std::string error;
    const auto devices = P75Controller::enumerate(error);
    if (devices.empty()) {
        std::puts(error.empty() ? "No PMO P75 HID interfaces found." : error.c_str());
        return 0;
    }
    for (const auto& device : devices) {
        std::printf("%04X:%04X usage %04X:%04X  %s\n",
            device.vendorId, device.productId, device.usagePage, device.usage,
            device.controlInterface ? "P75 control interface" : "other interface");
    }
    return 0;
}

int readDevice() {
    P75Controller controller;
    std::string error;
    const auto devices = P75Controller::enumerate(error);
    const auto found = std::find_if(devices.begin(), devices.end(),
        [](const HidCandidate& device) { return device.controlInterface; });
    if (found == devices.end()) {
        std::puts(error.empty() ? "P75 control interface not found." : error.c_str());
        return 1;
    }
    if (!controller.connect(found->path, error)) {
        std::fprintf(stderr, "Connect failed: %s\n", error.c_str());
        return 1;
    }
    DeviceSnapshot snapshot;
    if (!controller.readAll(snapshot, error)) {
        std::fprintf(stderr, "Read failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("P75 read succeeded: firmware=0x%04X protocol=%u type=%u matrix=%s\n",
        snapshot.firmwareVersion, snapshot.protocolVersion, snapshot.deviceType,
        snapshot.matrixScreenAvailable ? "yes" : "no");
    std::printf("Matrix metadata: flag=%u size=%u×%u\n",
        snapshot.matrixScreenFlag, snapshot.matrixScreenColumns,
        snapshot.matrixScreenRows);
    std::printf("Lighting: keys mode=%u brightness=%d, front mode=%u, sides mode=%u\n",
        snapshot.keyboard.mode, snapshot.keyboard.brightness,
        snapshot.front.mode, snapshot.sides.mode);
    std::printf("Matrix: mode=%u brightness=%d; custom image read=%zu RGB bytes\n",
        snapshot.matrix.mode, snapshot.matrix.brightness, p75::protocol::kMatrixBytes);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    bool selfTest = false;
    bool list = false;
    bool read = false;
    bool demo = false;
    bool smoke = false;
    for (int i = 1; i < argc; ++i) {
        selfTest |= std::strcmp(argv[i], "--self-test") == 0;
        list |= std::strcmp(argv[i], "--list-devices") == 0;
        read |= std::strcmp(argv[i], "--read-device") == 0;
        demo |= std::strcmp(argv[i], "--demo") == 0;
        smoke |= std::strcmp(argv[i], "--smoke-ui") == 0;
    }
    if (selfTest) return runSelfTest() ? 0 : 1;
    if (list) return listDevices();
    if (read) return readDevice();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("P75 Light Studio", 1120, 780,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        std::fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::fprintf(stderr, "Renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    configureStyle();
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer) ||
        !ImGui_ImplSDLRenderer3_Init(renderer)) {
        std::fprintf(stderr, "Dear ImGui SDL backend initialization failed.\n");
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    AppState app;
    app.demo = demo || smoke;
    app.smoke = smoke;
    app.smokeStart = std::chrono::steady_clock::now();
    if (app.demo) {
        app.loaded = true;
        app.snapshot.matrixScreenAvailable = true;
        app.snapshot.keyboard = LightAreaSettings{true, 4, 170, 3, Rgb{132, 89, 255}};
        app.snapshot.front = LightAreaSettings{true, 2, 120, 2, Rgb{255, 112, 196}};
        app.snapshot.sides = LightAreaSettings{true, 1, 140, 2, Rgb{65, 208, 240}};
        app.snapshot.matrix = MatrixSettings{};
        app.areaEditor = app.snapshot.keyboard;
        app.matrixEditor = app.snapshot.matrix;
        for (int x = 0; x < 7; ++x) {
            app.snapshot.pixels[static_cast<std::size_t>(3 * 7 + x)] = Rgb{128, 80, 255};
            app.snapshot.pixels[static_cast<std::size_t>(x * 7 + 3)] = Rgb{128, 80, 255};
        }
        app.status = "Preview mode is active; connect a P75 to write device settings.";
    } else {
        app.refresh();
    }

    bool running = true;
    bool renderFailed = false;
    while (running && app.open) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(window))) {
                running = false;
            }
        }
        app.consumeImageDialogResult();

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        drawWindow(app, window);
        ImGui::Render();

        SDL_SetRenderDrawColor(renderer, 14, 16, 21, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (!SDL_RenderPresent(renderer)) {
            std::fprintf(stderr, "Render failed: %s\n", SDL_GetError());
            renderFailed = true;
            break;
        }
        if (smoke && std::chrono::steady_clock::now() - app.smokeStart >
                         std::chrono::milliseconds(1800)) {
            running = false;
        }
    }

    if (smoke && !renderFailed) std::puts("UI render smoke test passed.");
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return renderFailed ? 1 : 0;
}

