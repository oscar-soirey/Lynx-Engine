// Lynx Editor (Raven) : hidden OpenGL context and GL -> Raven texture links.
// See GlBridge.h.

#include "GlBridge.h"

#include <glad/glad.h>
#include <raven/raven_vulkan.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace lynx::editor::gl
{
    namespace
    {
        HWND  g_window = nullptr;
        HDC   g_dc = nullptr;
        HGLRC g_context = nullptr;
        HMODULE g_opengl32 = nullptr;

        // GL_EXT_memory_object / GL_EXT_memory_object_win32 (not in glad 3.3).
        using PFNCreateMemoryObjects = void (APIENTRY*)(GLsizei, GLuint*);
        using PFNDeleteMemoryObjects = void (APIENTRY*)(GLsizei, const GLuint*);
        using PFNImportMemoryWin32Handle = void (APIENTRY*)(GLuint, GLuint64, GLenum, void*);
        using PFNTexStorageMem2D = void (APIENTRY*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, GLuint64);
        using PFNTexStorage2D = void (APIENTRY*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);

        PFNCreateMemoryObjects     pglCreateMemoryObjectsEXT = nullptr;
        PFNDeleteMemoryObjects     pglDeleteMemoryObjectsEXT = nullptr;
        PFNImportMemoryWin32Handle pglImportMemoryWin32HandleEXT = nullptr;
        PFNTexStorageMem2D         pglTexStorageMem2DEXT = nullptr;
        bool g_has_memory_object = false;

        constexpr GLenum kHandleTypeOpaqueWin32 = 0x9587;   // GL_HANDLE_TYPE_OPAQUE_WIN32_EXT

        using PFNwglCreateContextAttribsARB = HGLRC (WINAPI*)(HDC, HGLRC, const int*);

        bool HasExtension(const char* name)
        {
            GLint count = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &count);
            for (GLint i = 0; i < count; ++i)
            {
                const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
                if (ext && std::strcmp(ext, name) == 0)
                    return true;
            }
            return false;
        }

        void* LoadProc(const char* name)
        {
            return GetProcAddress(name);
        }
    }

    void* GetProcAddress(const char* name)
    {
        void* proc = reinterpret_cast<void*>(wglGetProcAddress(name));
        // wglGetProcAddress returns 0, 1, 2, 3 or -1 for the functions of GL 1.1.
        const intptr_t value = reinterpret_cast<intptr_t>(proc);
        if (value == 0 || value == 1 || value == 2 || value == 3 || value == -1)
        {
            if (!g_opengl32)
                g_opengl32 = LoadLibraryA("opengl32.dll");
            proc = g_opengl32 ? reinterpret_cast<void*>(::GetProcAddress(g_opengl32, name)) : nullptr;
        }
        return proc;
    }

    bool Init()
    {
        if (g_context)
            return true;

        WNDCLASSA wc = {};
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "LynxHiddenGL";
        RegisterClassA(&wc);

        g_window = CreateWindowExA(0, wc.lpszClassName, "Lynx GL", WS_OVERLAPPEDWINDOW,
                                   0, 0, 16, 16, nullptr, nullptr, wc.hInstance, nullptr);
        if (!g_window)
            return false;

        g_dc = GetDC(g_window);

        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.cStencilBits = 8;
        const int format = ChoosePixelFormat(g_dc, &pfd);
        if (!format || !SetPixelFormat(g_dc, format, &pfd))
            return false;

        // A legacy context first : it gives wglCreateContextAttribsARB.
        HGLRC legacy = wglCreateContext(g_dc);
        if (!legacy || !wglMakeCurrent(g_dc, legacy))
            return false;

        auto create_attribs = reinterpret_cast<PFNwglCreateContextAttribsARB>(
            wglGetProcAddress("wglCreateContextAttribsARB"));

        if (create_attribs)
        {
            const int attribs[] = {
                0x2091, 3,            // WGL_CONTEXT_MAJOR_VERSION_ARB
                0x2092, 3,            // WGL_CONTEXT_MINOR_VERSION_ARB
                0x9126, 0x00000001,   // WGL_CONTEXT_PROFILE_MASK_ARB : core
                0
            };
            g_context = create_attribs(g_dc, nullptr, attribs);
        }

        if (g_context)
        {
            wglMakeCurrent(g_dc, g_context);
            wglDeleteContext(legacy);
        }
        else
        {
            g_context = legacy;   // driver without the extension : keep the legacy one
        }

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(LoadProc)))
        {
            std::fprintf(stderr, "[GL] Could not load the OpenGL functions\n");
            return false;
        }

        // The scene is rendered off-screen : the hidden window never presents.
        using PFNwglSwapInterval = BOOL (WINAPI*)(int);
        if (auto swap = reinterpret_cast<PFNwglSwapInterval>(wglGetProcAddress("wglSwapIntervalEXT")))
            swap(0);

        if (HasExtension("GL_EXT_memory_object") && HasExtension("GL_EXT_memory_object_win32"))
        {
            pglCreateMemoryObjectsEXT = reinterpret_cast<PFNCreateMemoryObjects>(GetProcAddress("glCreateMemoryObjectsEXT"));
            pglDeleteMemoryObjectsEXT = reinterpret_cast<PFNDeleteMemoryObjects>(GetProcAddress("glDeleteMemoryObjectsEXT"));
            pglImportMemoryWin32HandleEXT = reinterpret_cast<PFNImportMemoryWin32Handle>(GetProcAddress("glImportMemoryWin32HandleEXT"));
            pglTexStorageMem2DEXT = reinterpret_cast<PFNTexStorageMem2D>(GetProcAddress("glTexStorageMem2DEXT"));
            g_has_memory_object = pglCreateMemoryObjectsEXT && pglDeleteMemoryObjectsEXT &&
                                  pglImportMemoryWin32HandleEXT && pglTexStorageMem2DEXT;
        }

        std::printf("[GL] %s, %s (shared textures : %s)\n",
                    reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                    reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                    g_has_memory_object ? "yes" : "no, copies through the CPU");
        return true;
    }

    void Shutdown()
    {
        if (g_context)
        {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(g_context);
            g_context = nullptr;
        }
        if (g_window)
        {
            ReleaseDC(g_window, g_dc);
            DestroyWindow(g_window);
            g_window = nullptr;
            g_dc = nullptr;
        }
    }


    // -------------------------------------------------------------------------
    // TextureLink
    // -------------------------------------------------------------------------

    TextureLink::~TextureLink()
    {
        Release();
    }

    void TextureLink::Release()
    {
        for (Slot& slot : slots_)
        {
            if (slot.fbo) glDeleteFramebuffers(1, &slot.fbo);
            if (slot.gl_texture) glDeleteTextures(1, &slot.gl_texture);
            if (slot.memory && pglDeleteMemoryObjectsEXT) pglDeleteMemoryObjectsEXT(1, &slot.memory);
            if (slot.texture != R_INVALID_ID && Ris_initialized()) Rtexture_destroy(slot.texture);
            slot = Slot{};
        }
        if (read_fbo_) glDeleteFramebuffers(1, &read_fbo_);
        read_fbo_ = 0;
        pbo_ = 0;
        width_ = height_ = 0;
        shown_ = R_INVALID_ID;
    }

    bool TextureLink::Recreate(int width, int height)
    {
        Release();
        width_ = width;
        height_ = height;
        glGenFramebuffers(1, &read_fbo_);

        shared_ = g_has_memory_object;

        for (Slot& slot : slots_)
        {
            if (shared_)
            {
                slot.texture = Rvulkan_texture_create_shared(width, height);
                RSharedTextureInfo info = {};
                if (slot.texture == R_INVALID_ID || !Rvulkan_texture_get_shared_info(slot.texture, &info))
                {
                    shared_ = false;
                }
                else
                {
                    pglCreateMemoryObjectsEXT(1, &slot.memory);
                    // GL takes the ownership of the handle.
                    pglImportMemoryWin32HandleEXT(slot.memory, info.allocation_size, kHandleTypeOpaqueWin32,
                                                  reinterpret_cast<void*>(info.handle));
                    glGenTextures(1, &slot.gl_texture);
                    glBindTexture(GL_TEXTURE_2D, slot.gl_texture);
                    pglTexStorageMem2DEXT(GL_TEXTURE_2D, 1, GL_RGBA8, width, height, slot.memory, 0);
                    glBindTexture(GL_TEXTURE_2D, 0);

                    glGenFramebuffers(1, &slot.fbo);
                    glBindFramebuffer(GL_FRAMEBUFFER, slot.fbo);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, slot.gl_texture, 0);
                    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    if (status != GL_FRAMEBUFFER_COMPLETE || glGetError() != GL_NO_ERROR)
                        shared_ = false;
                }
            }

            if (!shared_)
                break;
        }

        if (!shared_)
        {
            // CPU path : plain Raven textures, updated with the pixels.
            for (Slot& slot : slots_)
            {
                if (slot.fbo) glDeleteFramebuffers(1, &slot.fbo);
                if (slot.gl_texture) glDeleteTextures(1, &slot.gl_texture);
                if (slot.memory) pglDeleteMemoryObjectsEXT(1, &slot.memory);
                if (slot.texture != R_INVALID_ID) Rtexture_destroy(slot.texture);
                slot = Slot{};
            }
            std::vector<uint8_t> black(static_cast<size_t>(width) * height * 4, 0);
            slots_[0].texture = Rtexture_create_from_pixels(black.data(), width, height);
            if (slots_[0].texture == R_INVALID_ID)
                return false;
        }

        next_ = 0;
        return true;
    }

    bool TextureSize(unsigned int gl_texture, int& width, int& height)
    {
        width = height = 0;
        if (gl_texture == 0)
            return false;
        GLint previous = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
        glBindTexture(GL_TEXTURE_2D, gl_texture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
        return width > 0 && height > 0;
    }

    Rid TextureLink::Update(unsigned int gl_texture, int width, int height)
    {
        if (gl_texture == 0 || width <= 0 || height <= 0)
            return shown_ = R_INVALID_ID;

        if (width != width_ || height != height_ || slots_[0].texture == R_INVALID_ID)
        {
            if (!Recreate(width, height))
                return shown_ = R_INVALID_ID;
        }

        GLint previous_read = 0, previous_draw = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo_);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl_texture, 0);

        if (shared_)
        {
            Slot& slot = slots_[next_];
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, slot.fbo);
            // OpenGL renders bottom-up, Vulkan samples top-down : the blit flips.
            glBlitFramebuffer(0, 0, width, height, 0, height, width, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read));
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous_draw));
            // No semaphores between the two APIs : the copy is finished before
            // Raven may sample the image. The other slot is the one Raven shows.
            glFinish();
            shown_ = slot.texture;
            next_ ^= 1;
            return shown_;
        }

        static std::vector<uint8_t> pixels;
        static std::vector<uint8_t> flipped;
        pixels.resize(static_cast<size_t>(width) * height * 4);
        flipped.resize(pixels.size());
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous_draw));

        const size_t row = static_cast<size_t>(width) * 4;
        for (int y = 0; y < height; ++y)
            std::memcpy(flipped.data() + row * y, pixels.data() + row * (height - 1 - y), row);

        Rtexture_update(slots_[0].texture, flipped.data(), width, height);
        return shown_ = slots_[0].texture;
    }
}
