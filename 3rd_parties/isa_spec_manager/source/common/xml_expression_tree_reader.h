/*
 * Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
 */
#ifndef AMDISA_XML_EXPRESSION_TREE_READER_H_
#define AMDISA_XML_EXPRESSION_TREE_READER_H_

// C++ libraries.
#include <string>

// Forward declarations.
namespace tinyxml2
{
    class XMLElement;
}

namespace amdisa
{
    struct GenericExpressionNode;

    // Handles reading expression trees from XML parsed by tinyxml2.
    class XmlExpressionTreeReader
    {
    public:
        // Reads the tree from the first child of xml_elem.
        static bool ReadExpressionTree(const tinyxml2::XMLElement* xml_elem,
                                       GenericExpressionNode& expression_root,
                                       std::string& err_message);
    };
}  // namespace amdisa

#endif  // AMDISA_XML_EXPRESSION_TREE_READER_H_
