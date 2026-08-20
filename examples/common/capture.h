// capture.h - write a readback buffer to a 24-bit BMP (top-down), for screenshot
// verification of the examples. Input is BGRA8 (the swapchain format), which maps
// straight to BMP's BGR byte order. No dependency; examples only.
#pragma once

#include <cstdint>
#include <cstdio>

inline bool WriteBmpBGRA(const char* path, const uint8_t* bgra, uint32_t w, uint32_t h)
{
    std::FILE* f = std::fopen(path, "wb");
    if (!f)
        return false;
    const uint32_t rowSize  = (w * 3 + 3) & ~3u; // padded to 4 bytes
    const uint32_t dataSize = rowSize * h;
    const uint32_t fileSize = 54 + dataSize;

    auto u16 = [&](uint16_t v) {
        std::fputc(v & 0xff, f);
        std::fputc((v >> 8) & 0xff, f);
    };
    auto u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            std::fputc((v >> (i * 8)) & 0xff, f);
    };
    auto i32 = [&](int32_t v) { u32(static_cast<uint32_t>(v)); };

    // BITMAPFILEHEADER
    std::fputc('B', f);
    std::fputc('M', f);
    u32(fileSize);
    u16(0);
    u16(0);
    u32(54);
    // BITMAPINFOHEADER (negative height = top-down)
    u32(40);
    i32(static_cast<int32_t>(w));
    i32(-static_cast<int32_t>(h));
    u16(1);
    u16(24);
    u32(0);
    u32(dataSize);
    i32(2835);
    i32(2835);
    u32(0);
    u32(0);

    uint8_t pad[3] = {0, 0, 0};
    for (uint32_t y = 0; y < h; ++y)
    {
        const uint8_t* row = bgra + static_cast<size_t>(y) * w * 4;
        for (uint32_t x = 0; x < w; ++x)
        {
            std::fputc(row[x * 4 + 0], f);
            std::fputc(row[x * 4 + 1], f);
            std::fputc(row[x * 4 + 2], f);
        }
        std::fwrite(pad, 1, rowSize - w * 3, f);
    }
    std::fclose(f);
    return true;
}
