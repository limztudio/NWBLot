// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <functional>
#include <iterator>
#include <string>

#include "../allocation_size.h"
#include "../basic_string.h"
#include "../generic.h"
#include "../type.h"
#include "../type_properties.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemPathDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_DriveRootNameLength = 2u;
inline constexpr usize s_DotDotLength = 2u;
inline constexpr usize s_RelativeParentStepLength = 3u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
concept PathChar = SameAs<CharT, char> || SameAs<CharT, wchar> || SameAs<CharT, char8_t>;

template<typename SourceT>
concept PathSourceLike = CanMakeCharView<SourceT> || CanMakeWCharView<SourceT> || requires(const SourceT& source){ BasicStringView<char8_t>(source); };

template<typename CharT>
[[nodiscard]] inline b8 IsSeparator(const CharT ch)noexcept(IsArithmetic_V<CharT>){
    return ch == static_cast<CharT>('/')
#if defined(NWB_PLATFORM_WINDOWS)
        || ch == static_cast<CharT>('\\')
#endif
    ;
}

template<typename CharT>
[[nodiscard]] inline CharT PreferredSeparator()noexcept(IsArithmetic_V<CharT>){
#if defined(NWB_PLATFORM_WINDOWS)
    return static_cast<CharT>('\\');
#else
    return static_cast<CharT>('/');
#endif
}

template<typename CharT>
[[nodiscard]] inline usize RootNameLength(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
#if defined(NWB_PLATFORM_WINDOWS)
    if(text.size() >= s_DriveRootNameLength && text[1] == static_cast<CharT>(':'))
        return s_DriveRootNameLength;
    if(text.size() > 2u && IsSeparator(text[0]) && IsSeparator(text[1]) && !IsSeparator(text[2])){
        usize end = 2u;
        while(end < text.size() && !IsSeparator(text[end]))
            ++end;
        return end;
    }
#else
    static_cast<void>(text);
#endif
    return 0u;
}

template<typename CharT>
[[nodiscard]] inline usize RootDirectoryLength(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    const usize rootNameLength = RootNameLength(text);
    if(rootNameLength < text.size() && IsSeparator(text[rootNameLength]))
        return rootNameLength + 1u;
    if(rootNameLength == 0u && !text.empty() && IsSeparator(text[0]))
        return 1u;
    return 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline b8 IsDot(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    return text.size() == 1u && text[0] == static_cast<CharT>('.');
}

template<typename CharT>
[[nodiscard]] inline b8 IsDotDot(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    return text.size() == GlobalFilesystemPathDetail::s_DotDotLength && text[0] == static_cast<CharT>('.') && text[1] == static_cast<CharT>('.');
}


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
        Iterator(const Path& path, const b8 atEnd)noexcept
            : m_path(&path)
        {
            if(atEnd)
                moveToEnd();
            else
                moveToFirst();
        }


    public:
        [[nodiscard]] Path operator*()const{
            const native_string_view component = nativeComponent();
            return Path(m_path->arena(), component);
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

        [[nodiscard]] b8 operator==(const Iterator& rhs)const noexcept{
            return m_path == rhs.m_path && m_begin == rhs.m_begin && m_end == rhs.m_end && m_atEnd == rhs.m_atEnd;
        }

        [[nodiscard]] b8 operator!=(const Iterator& rhs)const noexcept{ return !(*this == rhs); }

        // Needs a non-end iterator. The view borrows the source path; mutating or destroying it invalidates the view.


    public:
        [[nodiscard]] native_string_view nativeComponent()const noexcept{
            NWB_ASSERT(m_path != nullptr && !m_atEnd);
            return native_string_view(m_path->m_text.data() + m_begin, m_end - m_begin);
        }


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

            const native_string_view text = m_path->native();
#if defined(NWB_PLATFORM_WINDOWS)
            const usize rootDirectoryLength = GlobalFilesystemPathDetail::RootDirectoryLength(text);
            if(rootDirectoryLength != 0u){
                m_begin = 0u;
                m_end = rootDirectoryLength;
                m_atEnd = false;
                return;
            }

            const usize rootNameLength = GlobalFilesystemPathDetail::RootNameLength(text);
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
        b8 m_atEnd = true;
    };


public:
    using iterator = Iterator;
    using const_iterator = Iterator;


public:
    explicit Path(ArenaT& arena)noexcept(noexcept(string_type(arena)))
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
    ~Path()noexcept = default;


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

    [[nodiscard]] operator const value_type*()const noexcept{ return c_str(); }

    template<typename SourceT>
    Path& operator+=(const SourceT& rhs){
        if constexpr(requires{ rhs.native(); }){
            const native_string_view view = rhs.native();
            if(!aliasesText(view)){
                m_text.append(view.data(), view.size());
                return *this;
            }
        }
        else if constexpr(requires{ native_string_view(rhs); }){
            const native_string_view view(rhs);
            if(!aliasesText(view)){
                m_text.append(view.data(), view.size());
                return *this;
            }
        }
        const Path rhsPath(arena(), rhs);
        m_text.append(rhsPath.native().data(), rhsPath.native().size());
        return *this;
    }

    template<typename SourceT>
    Path& operator/=(const SourceT& rhs){
        if constexpr(requires{ rhs.native(); }){
            const native_string_view view = rhs.native();
            if(!aliasesText(view)){
                if(view.empty())
                    return *this;
                if(IsAbsoluteView(view)){
                    m_text.assign(view.data(), view.size());
                    return *this;
                }
                appendComponent(view);
                return *this;
            }
        }
        else if constexpr(requires{ native_string_view(rhs); }){
            const native_string_view view(rhs);
            if(!aliasesText(view)){
                if(view.empty())
                    return *this;
                if(IsAbsoluteView(view)){
                    m_text.assign(view.data(), view.size());
                    return *this;
                }
                appendComponent(view);
                return *this;
            }
        }
        Path rhsPath(arena(), rhs);
        if(rhsPath.empty())
            return *this;
        if(rhsPath.isAbsolute()){
            m_text = rhsPath.m_text;
            return *this;
        }
        appendComponent(rhsPath.native());
        return *this;
    }


public:
    [[nodiscard]] ArenaT& arena()const noexcept{ return m_text.get_allocator().arena(); }

    [[nodiscard]] b8 empty()const noexcept{ return m_text.empty(); }

    [[nodiscard]] usize size()const noexcept{ return m_text.size(); }

    [[nodiscard]] const value_type* c_str()const noexcept{ return m_text.c_str(); }

    [[nodiscard]] const value_type* data()const noexcept{ return m_text.data(); }

    [[nodiscard]] native_string_view native()const noexcept{ return native_string_view(m_text.data(), m_text.size()); }


public:
    [[nodiscard]] std::string string()const{ return toStdString<char>(false); }

    [[nodiscard]] std::string genericString()const{ return toStdString<char>(true); }

    template<typename OutCharT>
    [[nodiscard]] std::basic_string<OutCharT> genericString()const{ return toStdString<OutCharT>(true); }

    [[nodiscard]] b8 isAbsolute()const noexcept{
        return IsAbsoluteView(native());
    }

    [[nodiscard]] b8 hasFilename()const noexcept{
        if(m_text.empty())
            return false;
        return !GlobalFilesystemPathDetail::IsSeparator(m_text.back());
    }

    [[nodiscard]] native_string_view parentPathView()const noexcept{
        if(m_text.empty())
            return native_string_view();

        const usize rootDirectoryLength = rootDirectoryLengthInText();
        const usize rootLength = rootDirectoryLength != 0u ? rootDirectoryLength : rootNameLengthInText();
        const usize end = trimTrailingSeparators(m_text.size());
        if(rootLength != 0u && end <= rootLength)
            return native_string_view(m_text.data(), rootLength);

        usize separator = end;
        while(separator > rootLength && !GlobalFilesystemPathDetail::IsSeparator(m_text[separator - 1u]))
            --separator;

        if(separator == 0u)
            return native_string_view();
        if(rootLength != 0u && separator <= rootLength)
            return native_string_view(m_text.data(), rootLength);

        return native_string_view(m_text.data(), trimTrailingSeparators(separator - 1u));
    }

    [[nodiscard]] Path parentPath()const &{
        const native_string_view view = parentPathView();
        return view.empty() ? Path(arena()) : Path(arena(), view);
    }

    [[nodiscard]] Path parentPath()&& noexcept{
        const usize parentLength = parentPathView().size();
        if(parentLength < m_text.size())
            m_text.erase(parentLength);
        return Move(*this);
    }

    [[nodiscard]] native_string_view filenameView()const noexcept{
        if(m_text.empty())
            return native_string_view();

        const usize rootNameLength = rootNameLengthInText();
        const usize rootDirectoryLength = rootDirectoryLengthInText();
        const usize end = trimTrailingSeparators(m_text.size());
        if((rootDirectoryLength != 0u && end <= rootDirectoryLength) || (rootNameLength != 0u && end <= rootNameLength))
            return native_string_view();

        usize begin = end;
        const usize boundary = rootDirectoryLength != 0u ? rootDirectoryLength : rootNameLength;
        while(begin > boundary && !GlobalFilesystemPathDetail::IsSeparator(m_text[begin - 1u]))
            --begin;
        return native_string_view(m_text.data() + begin, end - begin);
    }

    [[nodiscard]] Path filename()const{
        const native_string_view view = filenameView();
        return view.empty() ? Path(arena()) : Path(arena(), view);
    }

    [[nodiscard]] native_string_view stemView()const noexcept{
        const native_string_view fileName = trimmedFilename();
        const usize extensionBegin = fileName.rfind(static_cast<value_type>('.'));
        const usize stemSize = extensionBegin == native_string_view::npos || extensionBegin == 0u ? fileName.size() : extensionBegin;
        return fileName.substr(0u, stemSize);
    }

    [[nodiscard]] Path stem()const{
        const native_string_view view = stemView();
        return view.empty() ? Path(arena()) : Path(arena(), view);
    }

    [[nodiscard]] native_string_view extensionView()const noexcept{
        const native_string_view fileName = trimmedFilename();
        const usize extensionBegin = fileName.rfind(static_cast<value_type>('.'));
        if(extensionBegin == native_string_view::npos || extensionBegin == 0u)
            return native_string_view();
        return fileName.substr(extensionBegin);
    }

    [[nodiscard]] Path extension()const{
        const native_string_view view = extensionView();
        return view.empty() ? Path(arena()) : Path(arena(), view);
    }

    [[nodiscard]] Path lexicallyNormal()const{
        Path output(arena());
        output.m_text.reserve(AddSize(m_text.size(), 1u));
        appendNormalizedTo(output);
        return output;
    }

    [[nodiscard]] Path lexicallyRelative(const Path& base)const{
        if(isAbsolute() != base.isAbsolute())
            return Path(arena());
#if defined(NWB_PLATFORM_WINDOWS)
        if(native().substr(0u, rootNameLengthInText()) != base.native().substr(0u, base.rootNameLengthInText()))
            return Path(arena());
#endif

        Path output(arena());
        output.m_text.reserve(AddSize(AddSize(m_text.size(), SizeOf<GlobalFilesystemPathDetail::s_RelativeParentStepLength>(base.m_text.empty() ? 1u : base.m_text.size())), 1u));
        {
            const Path normalizedPath = lexicallyNormal();
            const Path normalizedBase = base.lexicallyNormal();
            Iterator pathIt = normalizedPath.begin();
            Iterator baseIt = normalizedBase.begin();
            const Iterator pathEnd = normalizedPath.end();
            const Iterator baseEnd = normalizedBase.end();

            while(pathIt != pathEnd && baseIt != baseEnd && pathIt.nativeComponent() == baseIt.nativeComponent()){
                ++pathIt;
                ++baseIt;
            }

            for(; baseIt != baseEnd; ++baseIt){
                const native_string_view component = baseIt.nativeComponent();
                if(component == native_string_view(NWB_TEXT("/"), 1u))
                    continue;
                output.appendComponent(native_string_view(NWB_TEXT(".."), GlobalFilesystemPathDetail::s_DotDotLength));
            }
            for(; pathIt != pathEnd; ++pathIt)
                output.appendComponent(pathIt.nativeComponent());
        }

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


public:
    Path& replaceExtension()noexcept{
        const usize extensionBegin = extensionBeginInText();
        if(extensionBegin < m_text.size())
            m_text.erase(extensionBegin);
        return *this;
    }

    template<typename SourceT>
    Path& replaceExtension(const SourceT& replacement){
        if constexpr(requires{ replacement.native(); }){
            const native_string_view view = replacement.native();
            if(!aliasesText(view)){
                replaceExtension();
                if(view.empty())
                    return *this;
                if(view.front() != static_cast<value_type>('.'))
                    m_text += static_cast<value_type>('.');
                m_text.append(view.data(), view.size());
                return *this;
            }
        }
        else if constexpr(requires{ native_string_view(replacement); }){
            const native_string_view view(replacement);
            if(!aliasesText(view)){
                replaceExtension();
                if(view.empty())
                    return *this;
                if(view.front() != static_cast<value_type>('.'))
                    m_text += static_cast<value_type>('.');
                m_text.append(view.data(), view.size());
                return *this;
            }
        }
        replaceExtension();
        Path replacementPath(arena(), replacement);
        if(replacementPath.empty())
            return *this;
        if(replacementPath.m_text.front() != static_cast<value_type>('.'))
            m_text += static_cast<value_type>('.');
        m_text += replacementPath.m_text;
        return *this;
    }


private:
    [[nodiscard]] static b8 IsAbsoluteView(const native_string_view text)noexcept{
        if(text.empty())
            return false;

#if defined(NWB_PLATFORM_WINDOWS)
        const usize rootNameLength = GlobalFilesystemPathDetail::RootNameLength(text);
        if(rootNameLength != 0u && GlobalFilesystemPathDetail::RootDirectoryLength(text) > rootNameLength)
            return true;
#endif
        return GlobalFilesystemPathDetail::IsSeparator(text[0]);
    }

    [[nodiscard]] b8 aliasesText(const native_string_view view)const noexcept{
        if(m_text.empty() || view.empty())
            return false;
        const usize textBegin = reinterpret_cast<usize>(m_text.data());
        const usize textEnd = reinterpret_cast<usize>(m_text.data() + m_text.size());
        const usize viewBegin = reinterpret_cast<usize>(view.data());
        const usize viewEnd = reinterpret_cast<usize>(view.data() + view.size());
        return viewBegin < textEnd && viewEnd > textBegin;
    }

    template<GlobalFilesystemPathDetail::PathChar CharT>
    void assignSource(const BasicStringView<CharT> source){
        if constexpr(SameAs<CharT, value_type>){
            m_text.assign(source.data(), source.size());
        }
        else if constexpr(SameAs<CharT, char8_t>){
            assignSource(AStringView(reinterpret_cast<const char*>(source.data()), source.size()));
        }
        else{
            m_text = ConvertString(arena(), source);
        }
    }

    template<typename SourceT>
    void assignSource(const SourceT& source){
        if constexpr(CanMakeCharView<SourceT>)
            assignSource(AStringView(source));
        else if constexpr(CanMakeWCharView<SourceT>)
            assignSource(WStringView(source));
        else
            assignSource(BasicStringView<char8_t>(source));
    }

    void appendComponent(const native_string_view component){
        if(component.empty())
            return;
        if(!m_text.empty() && !GlobalFilesystemPathDetail::IsSeparator(m_text.back())){
#if defined(NWB_PLATFORM_WINDOWS)
            if(m_text.size() == GlobalFilesystemPathDetail::s_DriveRootNameLength && m_text[1] == static_cast<value_type>(':')){
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

    [[nodiscard]] native_string_view trimmedFilename()const noexcept{
        const usize rootNameLength = rootNameLengthInText();
        const usize rootDirectoryLength = rootDirectoryLengthInText();
        const usize boundary = rootDirectoryLength != 0u ? rootDirectoryLength : rootNameLength;
        const usize end = trimTrailingSeparators(m_text.size());
        usize begin = end;
        while(begin > boundary && !GlobalFilesystemPathDetail::IsSeparator(m_text[begin - 1u]))
            --begin;
        return native_string_view(m_text.data() + begin, end - begin);
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
        const b8 absolute = isAbsolute();
        if(rootNameLength != 0u)
            output.m_text.append(m_text.data(), rootNameLength);
        if(rootDirectoryLength > rootNameLength)
            output.m_text += GlobalFilesystemPathDetail::PreferredSeparator<value_type>();

        for(Iterator component = begin(); component != end(); ++component){
            const native_string_view text = component.nativeComponent();
#if defined(NWB_PLATFORM_WINDOWS)
            if(rootDirectoryLength != 0u && text == native_string_view(m_text.data(), rootDirectoryLength))
                continue;
            if(rootDirectoryLength == 0u && rootNameLength != 0u && text == native_string_view(m_text.data(), rootNameLength))
                continue;
#endif
            if(text.empty() || IsDot(text))
                continue;
            if(text.size() == 1u && GlobalFilesystemPathDetail::IsSeparator(text[0]))
                continue;
            if(IsDotDot(text)){
                const native_string_view fileName = output.trimmedFilename();
                if(!fileName.empty() && !IsDotDot(fileName)){
                    const usize outputRootLength = output.rootDirectoryLengthInText();
                    usize separator = output.trimTrailingSeparators(output.m_text.size());
                    while(separator > outputRootLength && !GlobalFilesystemPathDetail::IsSeparator(output.m_text[separator - 1u]))
                        --separator;
                    const usize parentEnd = separator == 0u ? 0u : (outputRootLength != 0u && separator <= outputRootLength ? outputRootLength : output.trimTrailingSeparators(separator - 1u));
                    output.m_text.resize(parentEnd);
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
    [[nodiscard]] std::basic_string<OutCharT> toStdString(const b8 generic)const{
        std::basic_string<OutCharT> output;
        output.reserve(m_text.size());
        auto destination = BackInserter(output);
        if constexpr(SameAs<value_type, wchar> && !SameAs<OutCharT, wchar>)
            BasicStringDetail::WriteWStringAsUtf8(destination, native());
        else if constexpr(!SameAs<value_type, wchar> && SameAs<OutCharT, wchar>)
            BasicStringDetail::WriteUtf8AsWString(destination, native());
        else{
            for(const value_type ch : m_text)
                output.push_back(static_cast<OutCharT>(ch));
        }
        if(generic){
            for(OutCharT& ch : output){
                if(GlobalFilesystemPathDetail::IsSeparator(ch))
                    ch = static_cast<OutCharT>('/');
            }
        }
        return output;
    }


private:
    string_type m_text;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename LhsArenaT, typename RhsArenaT>
[[nodiscard]] inline b8 operator==(const Path<LhsArenaT>& lhs, const Path<RhsArenaT>& rhs)noexcept{ return lhs.native() == rhs.native(); }

template<typename LhsArenaT, typename RhsArenaT>
[[nodiscard]] inline b8 operator!=(const Path<LhsArenaT>& lhs, const Path<RhsArenaT>& rhs)noexcept{ return !(lhs == rhs); }

template<typename LhsArenaT, typename RhsArenaT>
[[nodiscard]] inline b8 operator<(const Path<LhsArenaT>& lhs, const Path<RhsArenaT>& rhs)noexcept{ return lhs.native() < rhs.native(); }

template<typename ArenaT, typename SourceT>
[[nodiscard]] inline Path<ArenaT> operator/(const Path<ArenaT>& lhs, const SourceT& rhs){
    Path<ArenaT> output(lhs);
    output /= rhs;
    return output;
}

template<typename ArenaT, typename SourceT>
[[nodiscard]] inline Path<ArenaT> operator/(Path<ArenaT>&& lhs, const SourceT& rhs){
    lhs /= rhs;
    return Move(lhs);
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

