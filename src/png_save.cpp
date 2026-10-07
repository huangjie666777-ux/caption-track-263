#include <png.h>

#include <cstdio>
#include <cstdint>
#include <vector>

#include "caption_track263/caption_track263.h"

namespace caption_track263 {

Error Engine::SavePng(const RasterImage& image, const std::string& path) {
  if (image.width <= 0 || image.height <= 0 ||
      image.pixels.size() !=
          static_cast<size_t>(image.width) * image.height * 4) {
    return Error::kFileIo;
  }

  FILE* fp = std::fopen(path.c_str(), "wb");
  if (!fp) return Error::kFileIo;

  png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr,
                              nullptr);
  if (!png) {
    std::fclose(fp);
    return Error::kFileIo;
  }
  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_write_struct(&png, nullptr);
    std::fclose(fp);
    return Error::kFileIo;
  }
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    std::fclose(fp);
    return Error::kFileIo;
  }

  png_init_io(png, fp);
  png_set_IHDR(png, info, static_cast<png_uint_32>(image.width),
               static_cast<png_uint_32>(image.height), 8,
               PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);

  std::vector<png_bytep> rows(image.height);
  for (int y = 0; y < image.height; ++y) {
    rows[y] = const_cast<png_bytep>(
        reinterpret_cast<const uint8_t*>(image.pixels.data()) +
        static_cast<size_t>(y) * image.width * 4);
  }
  png_write_image(png, rows.data());
  png_write_end(png, nullptr);

  png_destroy_write_struct(&png, &info);
  std::fclose(fp);
  return Error::kOk;
}

}  // namespace caption_track263
