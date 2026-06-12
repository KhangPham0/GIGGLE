#ifndef GIGGLE_UI_IMAGE_EXPORT_H
#define GIGGLE_UI_IMAGE_EXPORT_H

namespace giggle {

// Saves a rectangle of the current OpenGL back buffer as a PNG. The
// rectangle is in framebuffer pixels with a top-left origin (GL reads
// bottom-up; this function handles the flip). Call after rendering, before
// the buffer swap. Returns false when reading or writing failed.
bool SaveFramebufferRegionAsPng(const char* path, int x, int y, int width, int height,
                                int framebufferHeight);

} // namespace giggle

#endif
