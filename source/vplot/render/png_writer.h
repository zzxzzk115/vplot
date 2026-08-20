/*
 * Minimal PNG encoder for the raster backend's RGBA output. Writes the chunks
 * directly, depending only on zlib. Always 8-bit RGBA (colour type 6) with
 * filter type 0 on every scanline; adaptive filtering is not implemented.
 */

#ifndef VPL_RENDER_PNG_WRITER_H
#define VPL_RENDER_PNG_WRITER_H

#include <cstdint>
#include <string>
#include <vector>

namespace vpl
{

/* `rgba` must hold width * height * 4 bytes, row 0 first -- the layout
   AggRenderer::pixels() already produces. dpi, when positive, is recorded in a
   pHYs chunk so image viewers and LaTeX report the intended physical size. */
bool write_png(const std::string &path,
               const uint8_t *rgba,
               unsigned width,
               unsigned height,
               double dpi);

/* No read_png: decoding is only needed by the test harness, which uses
   stb_image, keeping the library's dependencies to zlib and FreeType. */

} // namespace vpl

#endif /* VPL_RENDER_PNG_WRITER_H */
