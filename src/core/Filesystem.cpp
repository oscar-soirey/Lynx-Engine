#include "filesystem.h"

#include <algorithm>
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

    std::filesystem::path GetExecutablePath()
    {
#ifdef _WIN32
        wchar_t buffer[MAX_PATH];

        const DWORD length =
            GetModuleFileNameW(nullptr, buffer, MAX_PATH);

        if (length == 0 || length >= MAX_PATH)
            return std::filesystem::path();

        return std::filesystem::path(buffer);
#else
        return std::filesystem::path();
#endif
    }

    // A zip "end of central directory" record in the last 64 KB of the file.
    bool FileEndsWithZip(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return false;

        const std::streamoff size = file.tellg();
        const std::streamoff tail = std::min<std::streamoff>(size, 22 + 65535);
        if (tail < 22)
            return false;

        std::vector<char> data(static_cast<size_t>(tail));
        file.seekg(size - tail);
        file.read(data.data(), tail);
        if (!file)
            return false;

        for (std::streamoff i = tail - 22; i >= 0; --i)
        {
            if (data[i] == 'P' && data[i + 1] == 'K' && data[i + 2] == 5 && data[i + 3] == 6)
                return true;
        }
        return false;
    }

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
            // Assets inside the executable (Ship Game), else assets.pak.
            const bool embedded = HasEmbeddedArchive();

            if (!embedded && !ArchiveExists())
                return false;

            if (!InitializePhysFS())
                return false;

            const auto archivePath =
                embedded ? GetExecutablePath() : GetExecutableDirectory() / "assets.pak";

            // PhysFS takes UTF-8 paths.
            const auto archive_utf8 = archivePath.u8string();
            const std::string archive_path(archive_utf8.begin(), archive_utf8.end());

            if (!PHYSFS_mount(
                    archive_path.c_str(),
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

    namespace
    {
        void ListArchive(const std::string& folder, bool recursive, std::vector<std::string>& out)
        {
            char** list = PHYSFS_enumerateFiles(folder.c_str());

            if (!list)
                return;

            for (char** it = list; *it; ++it)
            {
                const std::string path = folder.empty() ? *it : folder + "/" + *it;

                PHYSFS_Stat stat{};

                if (!PHYSFS_stat(path.c_str(), &stat))
                    continue;

                if (stat.filetype == PHYSFS_FILETYPE_DIRECTORY)
                {
                    if (recursive)
                        ListArchive(path, true, out);
                }
                else if (stat.filetype == PHYSFS_FILETYPE_REGULAR)
                {
                    out.push_back(path);
                }
            }

            PHYSFS_freeList(list);
        }
    }

    std::vector<std::string> ListFiles(const std::string& folder, bool recursive)
    {
        std::vector<std::string> out;

        if (!g_initialized || (!folder.empty() && !IsSafePath(folder)))
            return out;

        if (g_source == AssetSource::Directory)
        {
            const std::filesystem::path root = std::filesystem::current_path() / "assets";
            const std::filesystem::path dir = root / folder;
            std::error_code ec;

            if (!std::filesystem::is_directory(dir, ec))
                return out;

            auto add = [&](const std::filesystem::directory_entry& entry)
            {
                std::error_code e;

                if (entry.is_regular_file(e))
                    out.push_back(std::filesystem::relative(entry.path(), root, e).generic_string());
            };

            if (recursive)
            {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec))
                    add(entry);
            }
            else
            {
                for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
                    add(entry);
            }
        }
        else if (g_source == AssetSource::Archive)
        {
            ListArchive(folder, recursive, out);
        }

        std::sort(out.begin(), out.end());
        return out;
    }

    bool HasEmbeddedArchive()
    {
        static const bool embedded = [] {
            const std::filesystem::path exe = GetExecutablePath();
            return !exe.empty() && FileEndsWithZip(exe);
        }();
        return embedded;
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