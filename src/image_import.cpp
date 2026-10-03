#include "image_import.h"

#include <SDL3/SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

namespace p75::image {
namespace {

constexpr int kMatrixSide = 7;
constexpr std::size_t kMaxImagePixels = 32'000'000;

const std::array<double, 256>& linearSrgbLookup() {
    static const std::array<double, 256> lookup = [] {
        std::array<double, 256> values{};
        for (std::size_t i = 0; i < values.size(); ++i) {
            const double value = static_cast<double>(i) / 255.0;
            values[i] = value <= 0.04045
                ? value / 12.92
                : std::pow((value + 0.055) / 1.055, 2.4);
        }
        return values;
    }();
    return lookup;
}

double srgbToLinear(std::uint8_t channel) {
    return linearSrgbLookup()[channel];
}

std::uint8_t linearToSrgb(double value) {
    value = std::clamp(value, 0.0, 1.0);
    const double encoded = value <= 0.0031308
        ? value * 12.92
        : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
    return static_cast<std::uint8_t>(std::clamp(std::lround(encoded * 255.0), 0L, 255L));
}

bool hasSupportedExtension(const std::string& path) {
    const auto dot = path.find_last_of('.');
    const auto separator = path.find_last_of("/\\");
    if (dot == std::string::npos || (separator != std::string::npos && dot < separator)) {
        return false;
    }
    std::string extension = path.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == "jpg" || extension == "jpeg" || extension == "png";
}

} // namespace

bool convertRgbaToMatrix(const std::uint8_t* rgba, int width, int height,
                         FitMode fitMode, protocol::MatrixPixels& pixels,
                         std::string& error) {
    if (rgba == nullptr || width <= 0 || height <= 0) {
        error = "The selected image has no readable pixels.";
        return false;
    }

    const double sourceWidth = static_cast<double>(width);
    const double sourceHeight = static_cast<double>(height);
    double scale = 0.0;
    double offsetX = 0.0;
    double offsetY = 0.0;
    double cropX = 0.0;
    double cropY = 0.0;
    double cropSide = 0.0;

    if (fitMode == FitMode::CropToFill) {
        cropSide = std::min(sourceWidth, sourceHeight);
        cropX = (sourceWidth - cropSide) * 0.5;
        cropY = (sourceHeight - cropSide) * 0.5;
    } else {
        scale = static_cast<double>(kMatrixSide) / std::max(sourceWidth, sourceHeight);
        offsetX = (static_cast<double>(kMatrixSide) - sourceWidth * scale) * 0.5;
        offsetY = (static_cast<double>(kMatrixSide) - sourceHeight * scale) * 0.5;
    }

    for (int row = 0; row < kMatrixSide; ++row) {
        for (int col = 0; col < kMatrixSide; ++col) {
            double sourceX0 = 0.0;
            double sourceX1 = 0.0;
            double sourceY0 = 0.0;
            double sourceY1 = 0.0;
            if (fitMode == FitMode::CropToFill) {
                const double cell = cropSide / static_cast<double>(kMatrixSide);
                sourceX0 = cropX + static_cast<double>(col) * cell;
                sourceX1 = sourceX0 + cell;
                sourceY0 = cropY + static_cast<double>(row) * cell;
                sourceY1 = sourceY0 + cell;
            } else {
                sourceX0 = (static_cast<double>(col) - offsetX) / scale;
                sourceX1 = (static_cast<double>(col + 1) - offsetX) / scale;
                sourceY0 = (static_cast<double>(row) - offsetY) / scale;
                sourceY1 = (static_cast<double>(row + 1) - offsetY) / scale;
            }

            const double destinationArea = (sourceX1 - sourceX0) * (sourceY1 - sourceY0);
            double red = 0.0;
            double green = 0.0;
            double blue = 0.0;

            const double clippedX0 = std::clamp(sourceX0, 0.0, sourceWidth);
            const double clippedX1 = std::clamp(sourceX1, 0.0, sourceWidth);
            const double clippedY0 = std::clamp(sourceY0, 0.0, sourceHeight);
            const double clippedY1 = std::clamp(sourceY1, 0.0, sourceHeight);
            const int firstX = static_cast<int>(std::floor(clippedX0));
            const int endX = static_cast<int>(std::ceil(clippedX1));
            const int firstY = static_cast<int>(std::floor(clippedY0));
            const int endY = static_cast<int>(std::ceil(clippedY1));

            for (int y = firstY; y < endY; ++y) {
                const double weightY = std::max(0.0,
                    std::min(sourceY1, static_cast<double>(y + 1)) -
                    std::max(sourceY0, static_cast<double>(y)));
                for (int x = firstX; x < endX; ++x) {
                    const double weightX = std::max(0.0,
                        std::min(sourceX1, static_cast<double>(x + 1)) -
                        std::max(sourceX0, static_cast<double>(x)));
                    const double weight = weightX * weightY;
                    if (weight <= 0.0) continue;

                    const std::size_t index =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) * 4;
                    const double alpha = static_cast<double>(rgba[index + 3]) / 255.0;
                    red += weight * alpha * srgbToLinear(rgba[index]);
                    green += weight * alpha * srgbToLinear(rgba[index + 1]);
                    blue += weight * alpha * srgbToLinear(rgba[index + 2]);
                }
            }

            const double divisor = std::max(destinationArea, std::numeric_limits<double>::min());
            const std::size_t outputIndex = static_cast<std::size_t>(row * kMatrixSide + col);
            pixels[outputIndex] = protocol::Rgb{
                linearToSrgb(red / divisor),
                linearToSrgb(green / divisor),
                linearToSrgb(blue / divisor)
            };
        }
    }

    error.clear();
    return true;
}

bool loadImageToMatrix(const std::string& utf8Path, FitMode fitMode,
                       protocol::MatrixPixels& pixels, std::string& error) {
    if (!hasSupportedExtension(utf8Path)) {
        error = "Choose a JPG, JPEG, or PNG image.";
        return false;
    }

    std::size_t fileSize = 0;
    void* fileData = SDL_LoadFile(utf8Path.c_str(), &fileSize);
    if (fileData == nullptr) {
        error = std::string("Couldn't read that image: ") + SDL_GetError();
        return false;
    }
    const auto freeFileData = [](void* data) { SDL_free(data); };
    std::unique_ptr<void, decltype(freeFileData)> file(fileData, freeFileData);

    if (fileSize == 0 || fileSize > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        error = "The selected image file is empty or too large to decode.";
        return false;
    }

    const auto* bytes = static_cast<const stbi_uc*>(fileData);
    const int byteCount = static_cast<int>(fileSize);
    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    if (!stbi_info_from_memory(bytes, byteCount, &width, &height, &sourceChannels) ||
        width <= 0 || height <= 0) {
        const char* reason = stbi_failure_reason();
        error = std::string("Couldn't decode that image") +
            (reason != nullptr ? std::string(": ") + reason : ".");
        return false;
    }
    if (static_cast<std::size_t>(width) * static_cast<std::size_t>(height) > kMaxImagePixels) {
        error = "That image is very large. Please choose one up to 32 megapixels.";
        return false;
    }

    int decodedWidth = 0;
    int decodedHeight = 0;
    int decodedChannels = 0;
    stbi_uc* decoded = stbi_load_from_memory(bytes, byteCount,
        &decodedWidth, &decodedHeight, &decodedChannels, 4);
    if (decoded == nullptr) {
        const char* reason = stbi_failure_reason();
        error = std::string("Couldn't decode that image") +
            (reason != nullptr ? std::string(": ") + reason : ".");
        return false;
    }
    const auto freeDecoded = [](stbi_uc* data) { stbi_image_free(data); };
    std::unique_ptr<stbi_uc, decltype(freeDecoded)> rgba(decoded, freeDecoded);
    (void)sourceChannels;
    (void)decodedChannels;
    return convertRgbaToMatrix(rgba.get(), decodedWidth, decodedHeight,
                               fitMode, pixels, error);
}

} // namespace p75::image

