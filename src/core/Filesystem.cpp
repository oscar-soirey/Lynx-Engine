#include "filesystem.h"

#include <cstring>
#include <filesystem>
#include <fstream>

#include <physfs/physfs.h>

#ifdef _WIN32
    #include <windows.h>
#endif

namespace
{
    lynx::fs::AssetSource g_source;

    bool g_initialized = false;

    std::filesystem::path GetExecutableDirectory()
    {
#ifdef _WIN32
        char buffer[MAX_PATH];

        const DWORD length =
            GetModuleFileNameA(nullptr, buffer, MAX_PATH);

        if (length == 0 || length >= MAX_PATH)
            return std::filesystem::current_path();

        return std::filesystem::path(buffer).parent_path();
#else
        return std::filesystem::current_path();
#endif
    }

    bool DirectoryAssetsExist()
    {
        return std::filesystem::is_directory(
            std::filesystem::current_path() / "assets"
        );
    }

    bool ArchiveExists()
    {
        return std::filesystem::is_regular_file(
            GetExecutableDirectory() / "assets.pak"
        );
    }

    bool IsSafePath(const std::string& path)
    {
        if (path.empty())
            return false;

        // Asset paths are relative to the asset root.
        if (path[0] == '/' || path[0] == '\\')
            return false;

        // Reject Windows drive paths.
        if (path.find(':') != std::string::npos)
            return false;

        const std::filesystem::path filesystemPath(path);

        for (const auto& component : filesystemPath)
        {
            if (component == "..")
                return false;
        }

        return true;
    }

    bool InitializePhysFS()
    {
        if (PHYSFS_isInit())
            return true;

        return PHYSFS_init(nullptr) != 0;
    }
}

namespace lynx::fs
{
    bool Init(AssetSource source)
    {
        if (g_initialized)
            return true;

        if (source == AssetSource::Directory)
        {
            if (!DirectoryAssetsExist())
                return false;

            g_source = AssetSource::Directory;
            g_initialized = true;

            return true;
        }

        if (source == AssetSource::Archive)
        {
            if (!ArchiveExists())
                return false;

            if (!InitializePhysFS())
                return false;

            const auto archivePath =
                GetExecutableDirectory() / "assets.pak";

            if (!PHYSFS_mount(
                    archivePath.string().c_str(),
                    nullptr,
                    1))
            {
                return false;
            }

            g_source = AssetSource::Archive;
            g_initialized = true;

            return true;
        }

        return false;
    }

    bool Exists(const std::string& path)
    {
        if (!g_initialized || !IsSafePath(path))
            return false;

        if (g_source == AssetSource::Directory)
        {
            return std::filesystem::is_regular_file(
                std::filesystem::current_path()
                / "assets"
                / path
            );
        }

        if (g_source == AssetSource::Archive)
        {
            return PHYSFS_exists(path.c_str()) != 0;
        }

        return false;
    }

    std::vector<std::uint8_t> ReadBinary(
        const std::string& path)
    {
        if (!g_initialized || !IsSafePath(path))
            return {};

        if (g_source == AssetSource::Directory)
        {
            const auto filePath =
                std::filesystem::current_path()
                / "assets"
                / path;

            std::ifstream file(
                filePath,
                std::ios::binary | std::ios::ate
            );

            if (!file)
                return {};

            const std::streamsize size = file.tellg();

            if (size < 0)
                return {};

            file.seekg(0, std::ios::beg);

            std::vector<std::uint8_t> data(
                static_cast<std::size_t>(size)
            );

            if (size > 0)
            {
                if (!file.read(
                        reinterpret_cast<char*>(data.data()),
                        size))
                {
                    return {};
                }
            }

            return data;
        }

        if (g_source == AssetSource::Archive)
        {
            PHYSFS_File* file =
                PHYSFS_openRead(path.c_str());

            if (!file)
                return {};

            const PHYSFS_sint64 size =
                PHYSFS_fileLength(file);

            if (size < 0)
            {
                PHYSFS_close(file);
                return {};
            }

            std::vector<std::uint8_t> data(
                static_cast<std::size_t>(size)
            );

            if (size > 0)
            {
                const PHYSFS_sint64 read =
                    PHYSFS_readBytes(
                        file,
                        data.data(),
                        size
                    );

                if (read != size)
                {
                    PHYSFS_close(file);
                    return {};
                }
            }

            PHYSFS_close(file);

            return data;
        }

        return {};
    }

    bool ReadBinary(
        const std::string& path,
        void* buffer,
        std::size_t size)
    {
        if (buffer == nullptr && size != 0)
            return false;

        const auto data = ReadBinary(path);

        if (data.size() != size)
            return false;

        if (size != 0)
        {
            std::memcpy(
                buffer,
                data.data(),
                size
            );
        }

        return true;
    }

    AssetSource GetSource()
    {
        return g_source;
    }

    bool IsInitialized()
    {
        return g_initialized;
    }
}