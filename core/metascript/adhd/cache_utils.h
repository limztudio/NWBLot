// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/atomic.h>
#include <global/environment.h>
#include <global/filesystem.h>
#include <global/filesystem/operations.h>
#include <global/numeric_text.h>
#include <global/text_utils.h>

#if defined(FOUNDATION_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CacheDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CachedFileStamp{
    MString path;
    u64 fileSize = 0u;
    u64 mtimeLow = 0u;
    u64 mtimeHigh = 0u;

    explicit CachedFileStamp(MetaArena& arena)noexcept
        : path(arena)
    {}
};

inline void AddExistingUnique(MVector<MString>& list, MStringMap<u8>& seen, const MString& item){
    const MStringView view(item.data(), item.size());
    if(seen.find(view) != seen.end())
        return;
    seen.emplace(item, 1u);
    list.push_back(item);
}

[[nodiscard]] inline b8 QueryDiskFileStamp(const ::Path<MetaArena>& diskPath, u64& outSize, u64& outMtimeLow, u64& outMtimeHigh)noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    WIN32_FILE_ATTRIBUTE_DATA attrData{};
    if(!::GetFileAttributesExW(diskPath.c_str(), GetFileExInfoStandard, &attrData))
        return false;
    if((attrData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u)
        return false;
    outSize = (static_cast<u64>(attrData.nFileSizeHigh) << 32u) | static_cast<u64>(attrData.nFileSizeLow);
    outMtimeLow = static_cast<u64>(attrData.ftLastWriteTime.dwLowDateTime);
    outMtimeHigh = static_cast<u64>(attrData.ftLastWriteTime.dwHighDateTime);
    return true;
#else
    struct stat status{};
    if(::stat(diskPath.c_str(), &status) != 0 || S_ISDIR(status.st_mode))
        return false;
    outSize = static_cast<u64>(status.st_size);
    outMtimeLow = static_cast<u64>(status.st_mtime);
#if defined(__APPLE__)
    outMtimeHigh = static_cast<u64>(status.st_mtimespec.tv_nsec);
#elif defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L
    outMtimeHigh = static_cast<u64>(status.st_mtim.tv_nsec);
#else
    outMtimeHigh = 0u;
#endif
    return true;
#endif
}

[[nodiscard]] inline b8 QueryFileStamp(MetaArena& scratchArena, const MStringView utf8Path, CachedFileStamp& outStamp){
    if(utf8Path.empty())
        return false;
    const TString<MetaArena> nativePath = ConvertString(scratchArena, utf8Path);
    const ::Path<MetaArena> diskPath(scratchArena, TStringView(nativePath.data(), nativePath.size()));
    if(!QueryDiskFileStamp(diskPath, outStamp.fileSize, outStamp.mtimeLow, outStamp.mtimeHigh))
        return false;
    outStamp.path.assign(utf8Path.data(), utf8Path.size());
    return true;
}

[[nodiscard]] inline b8 VerifyCachedFileStamps(MetaArena& scratchArena, const Span<const CachedFileStamp> stamps){
    CachedFileStamp current(scratchArena);
    for(const CachedFileStamp& expected : stamps){
        if(!QueryFileStamp(scratchArena, MStringView(expected.path.data(), expected.path.size()), current))
            return false;
        if(current.fileSize != expected.fileSize || current.mtimeLow != expected.mtimeLow || current.mtimeHigh != expected.mtimeHigh)
            return false;
    }
    return true;
}

[[nodiscard]] inline MString NormalizeSlashes(MetaArena& arena, const MStringView raw){
    MString out(raw.data(), raw.size(), arena);
    for(MChar& ch : out){
        if(ch == '\\')
            ch = '/';
    }
    return out;
}

[[nodiscard]] inline ::Path<MetaArena> ResolveTempCachePath(MetaArena& arena, const MStringView fileName){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    wchar tempBuf[MAX_PATH + 2u] = {};
    const DWORD len = ::GetTempPathW(static_cast<DWORD>(MAX_PATH + 1u), tempBuf);
    if(len > 0u && len <= MAX_PATH){
        ::Path<MetaArena> dir(arena, TStringView(tempBuf, static_cast<usize>(len)));
        return dir / ::Path<MetaArena>(arena, fileName);
    }
    return ::Path<MetaArena>(arena, fileName);
#else
    const auto envTmp = ReadEnvironmentVariable(arena, "TMPDIR");
    const MStringView dirText = (envTmp && *envTmp && !(*envTmp)->empty())
        ? MStringView((*envTmp)->data(), (*envTmp)->size())
        : MStringView("/tmp");
    ::Path<MetaArena> dir(arena, dirText);
    return dir / ::Path<MetaArena>(arena, fileName);
#endif
}

inline void WriteBinaryFileAtomic(
    MetaArena& scratchArena,
    const ::Path<MetaArena>& targetPath,
    const Vector<u8, MetaArena>& bytes
){
    static Atomic<u64> s_TmpSeq{ 0u };
    const u64 seq = s_TmpSeq.fetch_add(1u, MemoryOrder::relaxed);
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    const u64 pid = static_cast<u64>(::GetCurrentProcessId());
    const u64 tid = static_cast<u64>(::GetCurrentThreadId());
#else
    const u64 pid = static_cast<u64>(::getpid());
    const u64 tid = 0u;
#endif
    const MString tmpName = StringFormat(
        scratchArena,
        "{}.tmp.{}.{}.{}",
        PathToGenericString<MChar>(scratchArena, targetPath),
        pid,
        tid,
        seq
    );
    const ::Path<MetaArena> tmpPath(scratchArena, MStringView(tmpName.data(), tmpName.size()));
    if(!WriteBinaryFile(tmpPath, bytes)){
        (void)RemoveFile(tmpPath);
        return;
    }
    if(!RenamePath(tmpPath, targetPath))
        (void)RemoveFile(tmpPath);
}


class BinaryBufferWriter final{
public:
    explicit BinaryBufferWriter(MetaArena& arena)
        : m_bytes(arena)
    {}


public:
    template<typename T>
    void writeValue(const T& val){
        static_assert(IsTriviallyCopyable_V<T>, "writeValue requires trivially copyable type");
        const u8* ptr = reinterpret_cast<const u8*>(&val);
        m_bytes.insert(m_bytes.end(), ptr, ptr + sizeof(T));
    }

    void writeBool(const b8 val){
        const u8 b = val ? 1u : 0u;
        writeValue(b);
    }

    void writeRawString(const MStringView sv){
        const u32 len = static_cast<u32>(sv.size());
        writeValue(len);
        if(len > 0u){
            const u8* ptr = reinterpret_cast<const u8*>(sv.data());
            m_bytes.insert(m_bytes.end(), ptr, ptr + len);
        }
    }

    [[nodiscard]] const Vector<u8, MetaArena>& bytes()const noexcept{ return m_bytes; }


private:
    Vector<u8, MetaArena> m_bytes;
};


class BinaryBufferReader final{
public:
    BinaryBufferReader(const u8* data, const usize size)noexcept
        : m_data(data)
        , m_size(size)
    {}


public:
    [[nodiscard]] b8 hasRemaining(const usize byteCount)const noexcept{
        return m_cursor <= m_size && byteCount <= (m_size - m_cursor);
    }

    template<typename T>
    [[nodiscard]] b8 readValue(T& outVal)noexcept{
        static_assert(IsTriviallyCopyable_V<T>, "readValue requires trivially copyable type");
        if(!hasRemaining(sizeof(T)))
            return false;
        FOUNDATION_MEMCPY(&outVal, sizeof(T), m_data + m_cursor, sizeof(T));
        m_cursor += sizeof(T);
        return true;
    }

    [[nodiscard]] b8 readBool(b8& outVal)noexcept{
        u8 b = 0u;
        if(!readValue(b))
            return false;
        outVal = (b != 0u);
        return true;
    }

    [[nodiscard]] b8 readRawStringView(MStringView& outView)noexcept{
        u32 len = 0u;
        if(!readValue(len) || !hasRemaining(len))
            return false;
        outView = MStringView(reinterpret_cast<const char*>(m_data + m_cursor), len);
        m_cursor += len;
        return true;
    }

    [[nodiscard]] b8 readString(MString& outStr){
        MStringView view;
        if(!readRawStringView(view))
            return false;
        outStr.assign(view.data(), view.size());
        return true;
    }

    [[nodiscard]] b8 readBytes(void* dst, const usize byteCount)noexcept{
        if(!hasRemaining(byteCount))
            return false;
        if(byteCount > 0u)
            FOUNDATION_MEMCPY(dst, byteCount, m_data + m_cursor, byteCount);
        m_cursor += byteCount;
        return true;
    }

    [[nodiscard]] b8 atEnd()const noexcept{ return m_cursor == m_size; }


private:
    const u8* m_data = nullptr;
    usize m_size = 0u;
    usize m_cursor = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

