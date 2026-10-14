#pragma once
// Lynx Editor (Raven) : the OpenGL side of the editor.
//
// Raven draws the editor in Vulkan, in its own native window. HRL keeps
// rendering in OpenGL 3.3, into an off-screen scene texture
// (HRL_GL_GetSceneTextureGL_ID), exactly as with the ImGui editor. This module:
//
//   - creates the OpenGL context on a hidden window (WGL, no GLFW) and gives
//     HRL its loader ;
//   - shows a GL texture in a Raven widget : the texture is copied GPU -> GPU
//     (glBlitFramebuffer, which also turns it upright) into an image that
//     Vulkan and OpenGL share (Rvulkan_texture_create_shared +
//     GL_EXT_memory_object_win32). Two images alternate so that Raven never
//     samples the one being written. Without the extension (GPU drivers that
//     do not export memory, two different GPUs), the copy goes through a pixel
//     buffer : slower, same result.

#include <Raven.h>
#include <cstdint>

namespace lynx::editor::gl
{
    // Creates the hidden window and its OpenGL 3.3 core context, current on
    // the calling (main) thread. false : no OpenGL, the editor cannot run.
    bool Init();
    void Shutdown();

    // For HRL_InitContext (the same signature as glfwGetProcAddress).
    void* GetProcAddress(const char* name);

    // Size of a GL texture (level 0) ; false when it is not a texture.
    bool TextureSize(unsigned int gl_texture, int& width, int& height);

    // One GL texture shown in Raven (scene of the Viewport, previews...).
    class TextureLink
    {
    public:
        TextureLink() = default;
        ~TextureLink();
        TextureLink(const TextureLink&) = delete;
        TextureLink& operator=(const TextureLink&) = delete;

        // Copies `gl_texture` (width x height, bottom-up as OpenGL renders it)
        // and returns the Raven texture to show this frame (Rviewport_set_texture,
        // Rimage_set_texture). R_INVALID_ID when there is nothing to show.
        Rid Update(unsigned int gl_texture, int width, int height);

        // Last texture returned by Update.
        Rid Texture() const { return shown_; }

        void Release();

    private:
        struct Slot
        {
            Rid texture = R_INVALID_ID;
            unsigned int memory = 0;      // GL memory object (shared path)
            unsigned int gl_texture = 0;  // GL view of the shared image
            unsigned int fbo = 0;
        };

        bool Recreate(int width, int height);

        Slot slots_[2];
        int next_ = 0;
        int width_ = 0;
        int height_ = 0;
        bool shared_ = false;     // false : read back through a pixel buffer
        unsigned int read_fbo_ = 0;
        unsigned int pbo_ = 0;
        Rid shown_ = R_INVALID_ID;
    };
}
