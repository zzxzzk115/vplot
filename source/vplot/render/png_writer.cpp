#include "png_writer.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

#include <zlib.h>

namespace vpl
{
namespace
{

uint32_t crc32_of(const uint8_t *data, std::size_t n)
{
    /* zlib already carries the table PNG's CRC needs. */
    return static_cast<uint32_t>(::crc32(::crc32(0L, nullptr, 0), data, static_cast<uInt>(n)));
}

void put_be32(std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void write_chunk(std::vector<uint8_t> &out, const char type[4], const std::vector<uint8_t> &data)
{
    put_be32(out, static_cast<uint32_t>(data.size()));

    /* The CRC covers the type and the payload, but not the length. */
    std::vector<uint8_t> crc_input;
    crc_input.reserve(4 + data.size());
    crc_input.insert(crc_input.end(), type, type + 4);
    crc_input.insert(crc_input.end(), data.begin(), data.end());

    out.insert(out.end(), crc_input.begin(), crc_input.end());
    put_be32(out, crc32_of(crc_input.data(), crc_input.size()));
}

} // namespace

bool write_png(const std::string &path,
               const uint8_t *rgba,
               unsigned width,
               unsigned height,
               double dpi)
{
    if (rgba == nullptr || width == 0 || height == 0) {
        return false;
    }

    /* Each scanline is prefixed with its filter type; 0 means "none". */
    const std::size_t stride = static_cast<std::size_t>(width) * 4u;
    std::vector<uint8_t> raw;
    raw.reserve((stride + 1) * height);
    for (unsigned y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + y * stride, rgba + (y + 1) * stride);
    }

    uLongf compressed_size = ::compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> compressed(compressed_size);
    if (::compress2(compressed.data(), &compressed_size, raw.data(),
                    static_cast<uLong>(raw.size()), 6) != Z_OK) {
        return false;
    }
    compressed.resize(compressed_size);

    std::vector<uint8_t> png;
    const uint8_t signature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    png.insert(png.end(), signature, signature + sizeof(signature));

    std::vector<uint8_t> ihdr;
    put_be32(ihdr, width);
    put_be32(ihdr, height);
    ihdr.push_back(8); /* bit depth */
    ihdr.push_back(6); /* colour type 6: truecolour with alpha */
    ihdr.push_back(0); /* deflate */
    ihdr.push_back(0); /* adaptive filtering */
    ihdr.push_back(0); /* no interlace */
    write_chunk(png, "IHDR", ihdr);

    if (dpi > 0.0) {
        /* pHYs is in pixels per metre. */
        const auto ppm = static_cast<uint32_t>(std::lround(dpi / 0.0254));
        std::vector<uint8_t> phys;
        put_be32(phys, ppm);
        put_be32(phys, ppm);
        phys.push_back(1); /* unit: metres */
        write_chunk(png, "pHYs", phys);
    }

    write_chunk(png, "IDAT", compressed);
    write_chunk(png, "IEND", {});

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    f.write(reinterpret_cast<const char *>(png.data()),
            static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(f);
}

} // namespace vpl
