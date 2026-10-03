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
#include <iterator>
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

struct KeyboardKey {
    float x;
    float y;
    float width;
    std::uint8_t lightIndex;
    const char* label;
};

// PMO Hub's P75 layout (layout/rdrp75.json), including its shared LED index
// for the Up and Left Ctrl keys.
constexpr KeyboardKey kP75Keys[] = {
    {0, 0, 1, 0, "Esc"}, {1.25f, 0, 1, 1, "F1"}, {2.25f, 0, 1, 2, "F2"},
    {3.25f, 0, 1, 3, "F3"}, {4.25f, 0, 1, 4, "F4"}, {5.5f, 0, 1, 5, "F5"},
    {6.5f, 0, 1, 6, "F6"}, {7.5f, 0, 1, 7, "F7"}, {8.5f, 0, 1, 8, "F8"},
    {9.75f, 0, 1, 9, "F9"}, {10.75f, 0, 1, 10, "F10"},
    {11.75f, 0, 1, 11, "F11"}, {12.75f, 0, 1, 12, "F12"},
    {14, 0, 1, 13, "Del"}, {15, 0, 1, 14, "Home"},
    {0, 1.25f, 1, 15, "~"}, {1, 1.25f, 1, 16, "1"}, {2, 1.25f, 1, 17, "2"},
    {3, 1.25f, 1, 18, "3"}, {4, 1.25f, 1, 19, "4"}, {5, 1.25f, 1, 20, "5"},
    {6, 1.25f, 1, 21, "6"}, {7, 1.25f, 1, 22, "7"}, {8, 1.25f, 1, 23, "8"},
    {9, 1.25f, 1, 24, "9"}, {10, 1.25f, 1, 25, "0"},
    {11, 1.25f, 1, 26, "-"}, {12, 1.25f, 1, 27, "="},
    {13, 1.25f, 2, 28, "Bksp"}, {15, 1.25f, 1, 29, "End"},
    {0, 2.25f, 1.5f, 30, "Tab"}, {1.5f, 2.25f, 1, 31, "Q"},
    {2.5f, 2.25f, 1, 32, "W"}, {3.5f, 2.25f, 1, 33, "E"},
    {4.5f, 2.25f, 1, 34, "R"}, {5.5f, 2.25f, 1, 35, "T"},
    {6.5f, 2.25f, 1, 36, "Y"}, {7.5f, 2.25f, 1, 37, "U"},
    {8.5f, 2.25f, 1, 38, "I"}, {9.5f, 2.25f, 1, 39, "O"},
    {10.5f, 2.25f, 1, 40, "P"}, {11.5f, 2.25f, 1, 41, "["},
    {12.5f, 2.25f, 1, 42, "]"}, {13.5f, 2.25f, 1.5f, 43, "\\"},
    {15, 2.25f, 1, 44, "PgUp"},
    {0, 3.25f, 1.75f, 45, "Caps"}, {1.75f, 3.25f, 1, 46, "A"},
    {2.75f, 3.25f, 1, 47, "S"}, {3.75f, 3.25f, 1, 48, "D"},
    {4.75f, 3.25f, 1, 49, "F"}, {5.75f, 3.25f, 1, 50, "G"},
    {6.75f, 3.25f, 1, 51, "H"}, {7.75f, 3.25f, 1, 52, "J"},
    {8.75f, 3.25f, 1, 53, "K"}, {9.75f, 3.25f, 1, 54, "L"},
    {10.75f, 3.25f, 1, 55, ";"}, {11.75f, 3.25f, 1, 56, "'"},
    {12.75f, 3.25f, 2.25f, 57, "Enter"}, {15, 3.25f, 1, 58, "PgDn"},
    {0, 4.25f, 2.25f, 59, "L-Shift"}, {2.25f, 4.25f, 1, 60, "Z"},
    {3.25f, 4.25f, 1, 61, "X"}, {4.25f, 4.25f, 1, 62, "C"},
    {5.25f, 4.25f, 1, 63, "V"}, {6.25f, 4.25f, 1, 64, "B"},
    {7.25f, 4.25f, 1, 65, "N"}, {8.25f, 4.25f, 1, 66, "M"},
    {9.25f, 4.25f, 1, 67, ","}, {10.25f, 4.25f, 1, 68, "."},
    {11.25f, 4.25f, 1, 69, "/"}, {12.25f, 4.25f, 1.75f, 70, "R-Shift"},
    {14, 4.25f, 1, 71, "↑"},
    {0, 5.25f, 1.25f, 71, "Ctrl"}, {1.25f, 5.25f, 1.25f, 72, "Win"},
    {2.5f, 5.25f, 1.25f, 73, "Alt"}, {3.75f, 5.25f, 6.25f, 74, "Space"},
    {10, 5.25f, 1.25f, 75, "Fn"}, {11.25f, 5.25f, 1.25f, 76, "Ctrl"},
    {13, 5.25f, 1, 77, "←"}, {14, 5.25f, 1, 78, "↓"},
    {15, 5.25f, 1, 79, "→"}
};

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
    Rgb keyboardBrush{132, 89, 255};
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
        status = snapshot.keyColorsAvailable
            ? "Read settings and custom key colors from the P75."
            : "Read settings. The active custom key-color profile could not be read; unpainted keys will be off when applied.";
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

    void applyKeyboardColors() {
        if (!loaded) {
            status = "Read the P75 settings before applying key colors.";
            return;
        }
        if (demo) {
            snapshot.keyboard.enabled = true;
            snapshot.keyboard.mode = p75::protocol::kCustomKeyLightMode;
            snapshot.keyColorsAvailable = true;
            if (selectedArea == 0) areaEditor = snapshot.keyboard;
            status = "Preview mode: the painted key colors are active in the local preview.";
            return;
        }
        std::string error;
        if (!controller.applyKeyboardKeyColors(snapshot.keyColors, snapshot, error)) {
            status = error;
            return;
        }
        snapshot.keyboard.enabled = true;
        snapshot.keyboard.mode = p75::protocol::kCustomKeyLightMode;
        snapshot.keyColorsAvailable = true;
        if (selectedArea == 0) areaEditor = snapshot.keyboard;
        status = "Applied the painted key colors to custom profile " +
                 std::to_string(snapshot.customLightSlot + 1) + ".";
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

void drawKeyboardPaintTab(AppState& app) {
    ImGui::TextWrapped("Choose a color, then click or drag across the keyboard to paint keys. Colors stay in the editor until you apply them.");
    ImGui::Spacing();

    float brush[3] = {app.keyboardBrush.r / 255.0f,
                      app.keyboardBrush.g / 255.0f,
                      app.keyboardBrush.b / 255.0f};
    if (ImGui::ColorEdit3("Paint color", brush,
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha)) {
        app.keyboardBrush = fromFloats(brush);
    }
    ImGui::SameLine();
    if (ImGui::Button("Fill all keys")) {
        for (const auto& key : kP75Keys) {
            app.snapshot.keyColors[key.lightIndex] = app.keyboardBrush;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear all keys")) {
        for (const auto& key : kP75Keys) {
            app.snapshot.keyColors[key.lightIndex] = Rgb{};
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Custom profile %d / 5", app.snapshot.customLightSlot + 1);

    ImGui::Separator();
    ImGui::TextUnformatted("P75 KEYBOARD");
    if (!app.snapshot.keyColorsAvailable && !app.demo) {
        ImGui::TextColored(ImVec4(1.0f, 0.73f, 0.38f, 1.0f),
            "The current profile could not be read. Unpainted keys will be set to black when applied.");
    }

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float unit = std::clamp((availableWidth - 28.0f) / 16.2f, 20.0f, 54.0f);
    const float boardHeight = 6.45f * unit + 12.0f;
    ImGui::BeginChild("keyboard-paint-canvas", ImVec2(0, boardHeight), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 boardStart = ImGui::GetCursorScreenPos();
    constexpr float inset = 8.0f;
    for (std::size_t index = 0; index < std::size(kP75Keys); ++index) {
        const auto& key = kP75Keys[index];
        const ImVec2 keyMin(boardStart.x + inset + key.x * unit,
                            boardStart.y + inset + key.y * unit);
        const ImVec2 keySize(std::max(14.0f, key.width * unit - 3.0f), unit - 3.0f);
        ImGui::SetCursorScreenPos(keyMin);
        ImGui::PushID(static_cast<int>(index));
        ImGui::InvisibleButton("key", keySize);
        const bool hovered = ImGui::IsItemHovered();
        if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            app.snapshot.keyColors[key.lightIndex] = app.keyboardBrush;
        }
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const Rgb color = app.snapshot.keyColors[key.lightIndex];
        const ImU32 fill = ImGui::ColorConvertFloat4ToU32(toImVec4(color));
        const ImU32 border = ImGui::GetColorU32(hovered
            ? ImVec4(0.94f, 0.82f, 1.0f, 1.0f)
            : ImVec4(0.35f, 0.37f, 0.43f, 1.0f));
        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, fill, 4.0f);
        drawList->AddRect(min, max, border, 4.0f, 0, hovered ? 2.0f : 1.0f);
        const float luminance = 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
        const ImU32 textColor = ImGui::GetColorU32(luminance > 145.0f
            ? ImVec4(0.06f, 0.07f, 0.09f, 1.0f)
            : ImVec4(0.97f, 0.97f, 1.0f, 1.0f));
        const ImVec2 labelSize = ImGui::CalcTextSize(key.label);
        const ImVec2 labelPos(min.x + (max.x - min.x - labelSize.x) * 0.5f,
                              min.y + (max.y - min.y - labelSize.y) * 0.5f);
        drawList->AddText(labelPos, textColor, key.label);
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::BeginDisabled(!app.loaded || (!app.demo && !app.controller.isConnected()));
    if (ImGui::Button("Apply key colors", ImVec2(190, 40))) app.applyKeyboardColors();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Applying selects the P75 custom static-color mode.");
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
        if (ImGui::BeginTabItem("Paint keys")) {
            ImGui::Spacing();
            drawKeyboardPaintTab(app);
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
    const std::vector<std::uint8_t> pngFixture{
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,4,0,0,
        0,181,28,12,2,0,0,0,11,73,68,65,84,120,156,99,96,96,0,0,0,3,0,1,104,38,
        89,13,0,0,0,0,73,69,78,68,174,66,96,130
    };
    const std::vector<std::uint8_t> jpegFixture{
        255,216,255,224,0,16,74,70,73,70,0,1,1,1,0,96,0,96,0,0,255,219,0,67,0,3,2,2,3,2,2,3,3,3,3,4,3,3,4,5,8,5,5,4,4,5,10,7,7,6,8,12,10,12,12,11,10,11,11,13,14,18,16,13,14,17,14,11,11,16,22,16,17,19,20,21,21,21,12,15,23,24,22,20,24,18,20,21,20,255,219,0,67,1,3,4,4,5,4,5,9,5,5,9,20,13,11,13,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,255,192,0,17,8,0,1,0,1,3,1,34,0,2,17,1,3,17,1,255,196,0,31,0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,255,196,0,181,16,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,125,1,2,3,0,4,17,5,18,33,49,65,6,19,81,97,7,34,113,20,50,129,145,161,8,35,66,177,193,21,82,209,240,36,51,98,114,130,9,10,22,23,24,25,26,37,38,39,40,41,42,52,53,54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,87,88,89,90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,131,132,133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,165,166,167,168,169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,199,200,201,202,210,211,212,213,214,215,216,217,218,225,226,227,228,229,230,231,232,233,234,241,242,243,244,245,246,247,248,249,250,255,196,0,31,1,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,255,196,0,181,17,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,119,0,1,2,3,17,4,5,33,49,6,18,65,81,7,97,113,19,34,50,129,8,20,66,145,161,177,193,9,35,51,82,240,21,98,114,209,10,22,36,52,225,37,241,23,24,25,26,38,39,40,41,42,53,54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,87,88,89,90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,130,131,132,133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,165,166,167,168,169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,199,200,201,202,210,211,212,213,214,215,216,217,218,226,227,228,229,230,231,232,233,234,242,243,244,245,246,247,248,249,250,255,218,0,12,3,1,0,2,17,3,17,0,63,0,249,210,138,40,175,195,15,245,76,255,217
    };
    MatrixPixels pngPixels{};
    MatrixPixels jpegPixels{};
    const auto sameColor = [](const Rgb& a, const Rgb& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    };
    if (!p75::image::decodeImageDataToMatrix(pngFixture.data(), pngFixture.size(),
            p75::image::FitMode::CropToFill, pngPixels, error) ||
        !std::all_of(pngPixels.begin(), pngPixels.end(),
            [&pngPixels, &sameColor](const Rgb& pixel) { return sameColor(pixel, pngPixels[0]); })) {
        std::fprintf(stderr, "self-test failed: PNG decode (%s)\n", error.c_str());
        return false;
    }
    if (!p75::image::decodeImageDataToMatrix(jpegFixture.data(), jpegFixture.size(),
            p75::image::FitMode::CropToFill, jpegPixels, error) ||
        !std::all_of(jpegPixels.begin(), jpegPixels.end(),
            [&jpegPixels, &sameColor](const Rgb& pixel) { return sameColor(pixel, jpegPixels[0]); })) {
        std::fprintf(stderr, "self-test failed: JPEG decode (%s)\n", error.c_str());
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
        app.snapshot.keyColorsAvailable = true;
        for (std::size_t i = 0; i < app.snapshot.keyColors.size(); ++i) {
            const float t = static_cast<float>(i) /
                            static_cast<float>(app.snapshot.keyColors.size() - 1);
            app.snapshot.keyColors[i] = Rgb{
                static_cast<std::uint8_t>(60 + 180 * t),
                static_cast<std::uint8_t>(80 + 80 * (1.0f - t)),
                static_cast<std::uint8_t>(210 - 130 * t)};
        }
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
