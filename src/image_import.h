#pragma once

#include "p75_protocol.h"

#include <cstdint>
#include <string>

namespace p75::image {

enum class FitMode {
    CropToFill,
    FitWithBlackBars
};

bool convertRgbaToMatrix(const std::uint8_t* rgba, int width, int height,
                         FitMode fitMode, protocol::MatrixPixels& pixels,
                         std::string& error);

bool loadImageToMatrix(const std::string& utf8Path, FitMode fitMode,
                       protocol::MatrixPixels& pixels, std::string& error);

} // namespace p75::image

