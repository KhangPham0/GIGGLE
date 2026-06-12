#include "ImageExport.h"

#include <vector>

#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace giggle {

bool SaveFramebufferRegionAsPng(const char* path, int x, int y, int width, int height,
                                int framebufferHeight)
{
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    // GL counts rows from the bottom; the rectangle comes in top-down.
    int glY = framebufferHeight - (y + height);

    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, glY, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Flip the rows so the file is top-down, and force full opacity (the
    // back buffer's alpha is meaningless for a screenshot).
    std::vector<unsigned char> flipped(pixels.size());
    int rowBytes = width * 4;
    for (int row = 0; row < height; ++row)
    {
        const unsigned char* source = pixels.data() + static_cast<size_t>(height - 1 - row) * rowBytes;
        unsigned char* target = flipped.data() + static_cast<size_t>(row) * rowBytes;
        for (int i = 0; i < rowBytes; i += 4)
        {
            target[i + 0] = source[i + 0];
            target[i + 1] = source[i + 1];
            target[i + 2] = source[i + 2];
            target[i + 3] = 255;
        }
    }

    return stbi_write_png(path, width, height, 4, flipped.data(), rowBytes) != 0;
}

} // namespace giggle
