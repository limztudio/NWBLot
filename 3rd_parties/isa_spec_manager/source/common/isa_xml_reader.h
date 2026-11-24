/*
 * Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
 */
#ifndef ISA_XML_READER_H_
#define ISA_XML_READER_H_

// C++ libraries.
#include <cstdint>
#include <vector>
#include <string>

namespace amdisa
{
    struct IsaSpec;
    struct EncodingCondition;

    class IsaXmlReader
    {
    public:
        // Reads a spec from a file. On failure, err_message describes the error.
        static bool ReadSpec(const std::string& path_to_input_xml, IsaSpec& spec_data, std::string& err_message);

        // Reads datalen bytes from memory. On failure, err_message describes the error.
        static bool ReadSpec(const char *input_xml_data, const size_t datalen, IsaSpec& spec_data, std::string& err_message);

        // Reads condition trees from a file. On failure, err_message describes the error.
        static bool ReadEncodingConditionExpressionTrees(const std::string& path_to_input_xml,
            std::vector<EncodingCondition>& encoding_conditions, uint32_t& architecture_id, std::string& err_message);

        IsaXmlReader()                               = delete;
        IsaXmlReader(const IsaXmlReader&)            = delete;
        IsaXmlReader& operator=(const IsaXmlReader&) = delete;
    };
}  // namespace amdisa
#endif  // ISA_XML_READER_H_
