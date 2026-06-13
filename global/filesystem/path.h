// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <functional>
#include <iterator>
#include <string>

#include "../basic_string.h"
#include "../generic.h"
#include "../type.h"
#include "../type_properties.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemPathDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
concept PathChar = SameAs<CharT, char> || SameAs<CharT, wchar>
#if defined(__cpp_char8_t)
    || SameAs<CharT, char8_t>
#endif
;

template<typename SourceT>
concept PathSourceLike =
    BasicStringDetail::CanMakeCharView<SourceT>
    || BasicStringDetail::CanMakeWCharView<SourceT>
;

template<typename CharT>
[[nodiscard]] inline bool IsSeparator(const CharT ch)noexcept{
    return ch == static_cast<CharT>('/')
#if defined(NWB_PLATFORM_WINDOWS)
        || ch == static_cast<CharT>('\\')
#endif
    ;
}

template<typename CharT>
[[nodiscard]] inline CharT PreferredSeparator()noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    return static_cast<CharT>('\\');
#else
    return static_cast<CharT>('/');
#endif
}

template<typename CharT>
[[nodiscard]] inline usize RootNameLength(const BasicStringView<CharT> text)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    if(text.size() >= 2u && text[1] == static_cast<CharT>(':'))
        return 2u;
#else
    static_cast<void>(text);
#endif
    return 0u;
}

template<typename CharT>
[[nodiscard]] inline usize RootDirectoryLength(const BasicStringView<CharT> text)noexcept{
    const usize rootNameLength = RootNameLength(text);
    if(rootNameLength < text.size() && IsSeparator(text[rootNameLength]))
        return rootNameLength + 1u;
    if(rootNameLength == 0u && !text.empty() && IsSeparator(text[0]))
        return 1u;
    return 0u;
}

template<typename CharT>
[[nodiscard]] inline bool IsDot(const BasicStringView<CharT> text)noexcept{
    return text.size() == 1u && text[0] == static_cast<CharT>('.');
}

template<typename CharT>
[[nodiscard]] inline bool IsDotDot(const BasicStringView<CharT> text)noexcept{
    return text.size() == 2u && text[0] == static_cast<CharT>('.') && text[1] == static_cast<CharT>('.');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
class Path{
public:
    using Arena = ArenaT;
    using value_type = tchar;
    using native_string_view = BasicStringView<value_type>;
    using string_type = BasicString<value_type, ArenaT>;


public:
    class Iterator{
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = Path;
        using PathChar = typename Path::value_type;
        using difference_type = isize;
        using pointer = void;
        using reference = Path;


    public:
        Iterator()noexcept = default;
        Iterator(const Path& path, const bool atEnd)noexcept
            : m_path(&path)
        {
            if(atEnd)
                moveToEnd();
            else
                moveToFirst();
        }


    public:
        [[nodiscard]] Path operator*()const{
            NWB_ASSERT(m_path != nullptr && !m_atEnd);
            return Path(m_path->arena(), native_string_view(m_path->m_text.data() + m_begin, m_end - m_begin));
        }

        Iterator& operator++()noexcept{
            moveToNext();
            return *this;
        }

        Iterator operator++(int)noexcept{
            Iterator copy = *this;
            moveToNext();
            return copy;
        }

        [[nodiscard]] bool operator==(const Iterator& rhs)const noexcept{
            return m_path == rhs.m_path && m_begin == rhs.m_begin && m_end == rhs.m_end && m_atEnd == rhs.m_atEnd;
        }

        [[nodiscard]] bool operator!=(const Iterator& rhs)const noexcept{ return !(*this == rhs); }


    private:
        void moveToEnd()noexcept{
            m_begin = m_path ? m_path->m_text.size() : 0u;
            m_end = m_begin;
            m_atEnd = true;
        }

        void moveToFirst()noexcept{
            if(m_path == nullptr || m_path->m_text.empty()){
                moveToEnd();
                return;
            }

            const PathChar* const text = m_path->m_text.data();
#if defined(NWB_PLATFORM_WINDOWS)
            const auto fullText = native_string_view(text, m_path->m_text.size());
            const usize rootDirectoryLength = GlobalFilesystemPathDetail::RootDirectoryLength(fullText);
            if(rootDirectoryLength != 0u){
                m_begin = 0u;
                m_end = rootDirectoryLength;
                m_atEnd = false;
                return;
            }

            const usize rootNameLength = GlobalFilesystemPathDetail::RootNameLength(fullText);
            if(rootNameLength != 0u){
                m_begin = 0u;
                m_end = rootNameLength;
                m_atEnd = false;
                return;
            }
#endif

            if(GlobalFilesystemPathDetail::IsSeparator(text[0])){
                m_begin = 0u;
                m_end = 1u;
                m_atEnd = false;
                return;
            }

            m_begin = 0u;
            m_end = findComponentEnd(m_begin);
            m_atEnd = m_begin == m_end;
        }

        void moveToNext()noexcept{
            if(m_path == nullptr || m_atEnd)
                return;

            usize nextBegin = m_end;
            while(nextBegin < m_path->m_text.size() && GlobalFilesystemPathDetail::IsSeparator(m_path->m_text[nextBegin]))
                ++nextBegin;

            if(nextBegin >= m_path->m_text.size()){
                moveToEnd();
                return;
            }

            m_begin = nextBegin;
            m_end = findComponentEnd(m_begin);
            m_atEnd = false;
        }

        [[nodiscard]] usize findComponentEnd(usize begin)const noexcept{
            while(begin < m_path->m_text.size() && !GlobalFilesystemPathDetail::IsSeparator(m_path->m_text[begin]))
                ++begin;
            return begin;
        }


    private:
        const Path* m_path = nullptr;
        usize m_begin = 0u;
        usize m_end = 0u;
        bool m_atEnd = true;
    };
    using iterator = Iterator;
    using const_iterator = Iterator;


public:
    explicit Path(ArenaT& arena)
        : m_text(arena)
    {}
    Path(const Path&) = default;
    Path(Path&&)noexcept = default;
    explicit Path(ArenaT& arena, const Path& source)
        : m_text(source.native(), arena)
    {}
    template<typename OtherArenaT>
    explicit Path(ArenaT& arena, const Path<OtherArenaT>& source)
        : m_text(source.native(), arena)
    {}
    template<GlobalFilesystemPathDetail::PathChar CharT>
    explicit Path(ArenaT& arena, const BasicStringView<CharT> source)
        : Path(arena)
    {
        assignSource(source);
    }
    template<typename SourceT> requires (GlobalFilesystemPathDetail::PathSourceLike<SourceT> && !SameAs<Decay_T<SourceT>, Path>)
    explicit Path(ArenaT& arena, const SourceT& source)
        : Path(arena)
    {
        assignSource(source);
    }
    ~Path() = default;


public:
    Path& operator=(const Path&) = default;
    Path& operator=(Path&&)noexcept = default;

    template<typename OtherArenaT>
    Path& operator=(const Path<OtherArenaT>& rhs){
        m_text.assign(rhs.native().data(), rhs.native().size());
        return *this;
    }

    template<typename SourceT> requires (GlobalFilesystemPathDetail::PathSourceLike<SourceT> && !SameAs<Decay_T<SourceT>, Path>)
    Path& operator=(const SourceT& rhs){
        assignSource(rhs);
        return *this;
    }


public:
    [[nodiscard]] ArenaT& arena()const noexcept{ return m_text.get_allocator().arena(); }
    [[nodiscard]] bool empty()const noexcept{ return m_text.empty(); }
    [[nodiscard]] usize size()const noexcept{ return m_text.size(); }
    [[nodiscard]] const value_type* c_str()const noexcept{ return m_text.c_str(); }
    [[nodiscard]] const value_type* data()const noexcept{ return m_text.data(); }
    [[nodiscard]] native_string_view native()const noexcept{ return native_string_view(m_text.data(), m_text.size()); }
    [[nodiscard]] operator const value_type*()const noexcept{ return c_str(); }

    [[nodiscard]] std::string string()const{ return toStdString<char>(false); }
    [[nodiscard]] std::string generic_string()const{ return toStdString<char>(true); }
    template<typename OutCharT>
    [[nodiscard]] std::basic_string<OutCharT> generic_string()const{ return toStdString<OutCharT>(true); }
    [[nodiscard]] std::wstring generic_wstring()const{ return toStdString<wchar>(true); }
    [[nodiscard]] auto generic_u8string()const{
        const std::string text = generic_string();
#if defined(__cpp_char8_t)
        return std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size());
#else
        return text;
#endif
    }

    [[nodiscard]] bool is_absolute()const noexcept{
        if(m_text.empty())
            return false;

#if defined(NWB_PLATFORM_WINDOWS)
        const usize rootNameLength = rootNameLengthInText();
        if(rootNameLength != 0u && rootDirectoryLengthInText() > rootNameLength)
            return true;
#endif
        return GlobalFilesystemPathDetail::IsSeparator(m_text[0]);
    }

    [[nodiscard]] bool has_filename()const noexcept{
        if(m_text.empty())
            return false;
        return !GlobalFilesystemPathDetail::IsSeparator(m_text.back());
    }

    [[nodiscard]] Path parent_path()const{
        if(m_text.empty())
            return Path(arena());

        const usize rootDirectoryLength = rootDirectoryLengthInText();
        usize end = trimTrailingSeparators(m_text.size());
        if(rootDirectoryLength != 0u && end <= rootDirectoryLength)
            return Path(arena(), native_string_view(m_text.data(), rootDirectoryLength));

        usize separator = end;
        while(separator > rootDirectoryLength && !GlobalFilesystemPathDetail::IsSeparator(m_text[separator - 1u]))
            --separator;

        if(separator == 0u)
            return Path(arena());
        if(rootDirectoryLength != 0u && separator <= rootDirectoryLength)
            return Path(arena(), native_string_view(m_text.data(), rootDirectoryLength));

        return Path(arena(), native_string_view(m_text.data(), trimTrailingSeparators(separator - 1u)));
    }

    [[nodiscard]] Path filename()const{
        if(m_text.empty())
            return Path(arena());

        const usize rootNameLength = rootNameLengthInText();
        const usize rootDirectoryLength = rootDirectoryLengthInText();
        const usize end = trimTrailingSeparators(m_text.size());
        if((rootDirectoryLength != 0u && end <= rootDirectoryLength) || (rootNameLength != 0u && end <= rootNameLength))
            return Path(arena());

        usize begin = end;
        const usize boundary = rootDirectoryLength != 0u ? rootDirectoryLength : rootNameLength;
        while(begin > boundary && !GlobalFilesystemPathDetail::IsSeparator(m_text[begin - 1u]))
            --begin;
        return Path(arena(), native_string_view(m_text.data() + begin, end - begin));
    }

    [[nodiscard]] Path stem()const{
        const Path fileName = filename();
        const usize extensionBegin = fileName.extensionBegin();
        if(extensionBegin == fileName.m_text.size())
            return fileName;
        return Path(arena(), native_string_view(fileName.m_text.data(), extensionBegin));
    }

    [[nodiscard]] Path extension()const{
        const Path fileName = filename();
        const usize extensionBegin = fileName.extensionBegin();
        if(extensionBegin == fileName.m_text.size())
            return Path(arena());
        return Path(arena(), native_string_view(fileName.m_text.data() + extensionBegin, fileName.m_text.size() - extensionBegin));
    }

    [[nodiscard]] Path lexically_normal()const{
        Path output(arena());
        appendNormalizedTo(output);
        return output;
    }

    [[nodiscard]] Path lexically_relative(const Path& base)const{
        const Path normalizedPath = lexically_normal();
        const Path normalizedBase = base.lexically_normal();
        if(normalizedPath.is_absolute() != normalizedBase.is_absolute())
            return Path(arena());

        Path output(arena());
        Iterator pathIt = normalizedPath.begin();
        Iterator baseIt = normalizedBase.begin();
        const Iterator pathEnd = normalizedPath.end();
        const Iterator baseEnd = normalizedBase.end();

        while(pathIt != pathEnd && baseIt != baseEnd && *pathIt == *baseIt){
            ++pathIt;
            ++baseIt;
        }

        for(; baseIt != baseEnd; ++baseIt){
            const Path component = *baseIt;
            if(component.native() == native_string_view(NWB_TEXT("/"), 1u))
                continue;
            output.appendComponent(native_string_view(NWB_TEXT(".."), 2u));
        }
        for(; pathIt != pathEnd; ++pathIt)
            output.appendComponent((*pathIt).native());

        if(output.empty())
            output.assignSource(native_string_view(NWB_TEXT("."), 1u));
        return output;
    }

    [[nodiscard]] iterator begin()const noexcept{ return iterator(*this, false); }
    [[nodiscard]] iterator end()const noexcept{ return iterator(*this, true); }


public:
    void clear()noexcept{ m_text.clear(); }

    template<typename SourceT>
    Path& append(const SourceT& rhs){ return (*this /= rhs); }

    template<typename SourceT>
    Path& operator+=(const SourceT& rhs){
        const Path rhsPath(arena(), rhs);
        m_text.append(rhsPath.native().data(), rhsPath.native().size());
        return *this;
    }

    Path& replace_extension(){
        const usize extensionBegin = extensionBeginInText();
        if(extensionBegin < m_text.size())
            m_text.erase(extensionBegin);
        return *this;
    }

    template<typename SourceT>
    Path& replace_extension(const SourceT& replacement){
        replace_extension();
        Path replacementPath(arena(), replacement);
        if(replacementPath.empty())
            return *this;
        if(replacementPath.m_text.front() != static_cast<value_type>('.'))
            m_text += static_cast<value_type>('.');
        m_text += replacementPath.m_text;
        return *this;
    }

    template<typename SourceT>
    Path& operator/=(const SourceT& rhs){
        Path rhsPath(arena(), rhs);
        if(rhsPath.empty())
            return *this;
        if(rhsPath.is_absolute()){
            m_text = rhsPath.m_text;
            return *this;
        }
        appendComponent(rhsPath.native());
        return *this;
    }


private:
    template<GlobalFilesystemPathDetail::PathChar CharT>
    void assignSource(const BasicStringView<CharT> source){
        if constexpr(SameAs<CharT, value_type>){
            m_text.assign(source.data(), source.size());
        }
        else{
            m_text = StringConvert(arena(), source);
        }
    }

    template<typename SourceT>
    void assignSource(const SourceT& source){
        if constexpr(BasicStringDetail::CanMakeCharView<SourceT>)
            assignSource(AStringView(source));
        else
            assignSource(WStringView(source));
    }

    void appendComponent(const native_string_view component){
        if(component.empty())
            return;
        if(!m_text.empty() && !GlobalFilesystemPathDetail::IsSeparator(m_text.back())){
#if defined(NWB_PLATFORM_WINDOWS)
            if(m_text.size() == rootNameLengthInText()){
                m_text.append(component.data(), component.size());
                return;
            }
#endif
            m_text += GlobalFilesystemPathDetail::PreferredSeparator<value_type>();
        }
        m_text.append(component.data(), component.size());
    }

    [[nodiscard]] usize trimTrailingSeparators(usize end)const noexcept{
        const usize rootDirectoryLength = rootDirectoryLengthInText();
        while(end > rootDirectoryLength && end > 0u && GlobalFilesystemPathDetail::IsSeparator(m_text[end - 1u]))
            --end;
        return end;
    }

    [[nodiscard]] usize extensionBegin()const noexcept{ return extensionBeginInText(); }

    [[nodiscard]] usize extensionBeginInText()const noexcept{
        if(m_text.empty())
            return m_text.size();

        const usize fileBegin = filenameBeginInText();
        for(usize i = m_text.size(); i > fileBegin; --i){
            if(m_text[i - 1u] == static_cast<value_type>('.')){
                if(i - 1u == fileBegin)
                    return m_text.size();
                return i - 1u;
            }
        }
        return m_text.size();
    }

    [[nodiscard]] usize filenameBeginInText()const noexcept{
        const usize rootNameLength = rootNameLengthInText();
        const usize rootDirectoryLength = rootDirectoryLengthInText();
        const usize boundary = rootDirectoryLength != 0u ? rootDirectoryLength : rootNameLength;
        usize begin = m_text.size();
        while(begin > boundary && !GlobalFilesystemPathDetail::IsSeparator(m_text[begin - 1u]))
            --begin;
        return begin;
    }

    void appendNormalizedTo(Path& output)const{
        const usize rootNameLength = rootNameLengthInText();
        const usize rootDirectoryLength = rootDirectoryLengthInText();
        const bool absolute = is_absolute();
        if(rootNameLength != 0u)
            output.m_text.append(m_text.data(), rootNameLength);
        if(rootDirectoryLength > rootNameLength)
            output.m_text += GlobalFilesystemPathDetail::PreferredSeparator<value_type>();

        for(const Path component : *this){
            const native_string_view text = component.native();
#if defined(NWB_PLATFORM_WINDOWS)
            if(rootDirectoryLength != 0u && text == native_string_view(m_text.data(), rootDirectoryLength))
                continue;
            if(rootDirectoryLength == 0u && rootNameLength != 0u && text == native_string_view(m_text.data(), rootNameLength))
                continue;
#endif
            if(text.empty() || GlobalFilesystemPathDetail::IsDot(text))
                continue;
            if(text.size() == 1u && GlobalFilesystemPathDetail::IsSeparator(text[0]))
                continue;
            if(GlobalFilesystemPathDetail::IsDotDot(text)){
                if(!output.empty() && !output.filename().empty() && !GlobalFilesystemPathDetail::IsDotDot(output.filename().native())){
                    output = output.parent_path();
                    continue;
                }
                if(!absolute)
                    output.appendComponent(text);
                continue;
            }
            output.appendComponent(text);
        }

        if(output.empty() && !absolute)
            output.m_text += static_cast<value_type>('.');
    }

    [[nodiscard]] usize rootNameLengthInText()const noexcept{
        return GlobalFilesystemPathDetail::RootNameLength(native());
    }

    [[nodiscard]] usize rootDirectoryLengthInText()const noexcept{
        return GlobalFilesystemPathDetail::RootDirectoryLength(native());
    }

    template<typename OutCharT>
    [[nodiscard]] std::basic_string<OutCharT> toStdString(const bool generic)const{
        std::basic_string<OutCharT> output;
        output.reserve(m_text.size());
        for(const value_type ch : m_text){
            value_type normalized = ch;
            if(generic && GlobalFilesystemPathDetail::IsSeparator(normalized))
                normalized = static_cast<value_type>('/');
            output.push_back(static_cast<OutCharT>(normalized));
        }
        return output;
    }


private:
    string_type m_text;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename LhsArenaT, typename RhsArenaT>
[[nodiscard]] inline bool operator==(const Path<LhsArenaT>& lhs, const Path<RhsArenaT>& rhs)noexcept{ return lhs.native() == rhs.native(); }
template<typename LhsArenaT, typename RhsArenaT>
[[nodiscard]] inline bool operator!=(const Path<LhsArenaT>& lhs, const Path<RhsArenaT>& rhs)noexcept{ return !(lhs == rhs); }
template<typename LhsArenaT, typename RhsArenaT>
[[nodiscard]] inline bool operator<(const Path<LhsArenaT>& lhs, const Path<RhsArenaT>& rhs)noexcept{ return lhs.native() < rhs.native(); }

template<typename ArenaT, typename SourceT>
[[nodiscard]] inline Path<ArenaT> operator/(const Path<ArenaT>& lhs, const SourceT& rhs){
    Path<ArenaT> output(lhs);
    output /= rhs;
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
struct hash<Path<ArenaT>>{
    [[nodiscard]] size_t operator()(const Path<ArenaT>& path)const noexcept{
        return hash<typename Path<ArenaT>::native_string_view>{}(path.native());
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

