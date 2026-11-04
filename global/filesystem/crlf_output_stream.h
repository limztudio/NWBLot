// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations.h"

#include <global/type_properties.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Normalize each written text chunk to CRLF without buffering the full output.
class CrlfOutputFileStream final : NoCopy{
public:
    explicit CrlfOutputFileStream(BasicOutputFileStream<char>& stream)noexcept
        : m_stream(stream)
    {}


public:
    template<usize Length>
    CrlfOutputFileStream& operator<<(const char (&text)[Length]){
        writeText(AStringView(text, Length - 1u));
        return *this;
    }

    CrlfOutputFileStream& operator<<(const AStringView text){
        writeText(text);
        return *this;
    }

    template<typename T>
        requires(!IsConvertible_V<const T&, AStringView>)
    CrlfOutputFileStream& operator<<(const T& value){
        m_stream << value;
        return *this;
    }


public:
    [[nodiscard]] explicit operator b8()const noexcept{ return static_cast<b8>(m_stream); }

    [[nodiscard]] b8 operator!()const noexcept{ return !m_stream; }


public:
    void precision(const StreamSize precision)noexcept{ m_stream.precision(precision); }


private:
    void writeText(const AStringView text){
        usize chunkBegin = 0u;
        for(usize i = 0u; i < text.size(); ++i){
            const char character = text[i];
            if(character != '\r' && character != '\n')
                continue;

            if(i > chunkBegin)
                m_stream.write(text.data() + chunkBegin, static_cast<StreamSize>(i - chunkBegin));

            if(character == '\r' && i + 1u < text.size() && text[i + 1u] == '\n')
                ++i;
            m_stream.write("\r\n", static_cast<StreamSize>(2u));
            chunkBegin = i + 1u;
        }

        if(chunkBegin < text.size())
            m_stream.write(text.data() + chunkBegin, static_cast<StreamSize>(text.size() - chunkBegin));
    }


private:
    BasicOutputFileStream<char>& m_stream;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

