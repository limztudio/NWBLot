/*
 * Copyright (c) 2024 Advanced Micro Devices, Inc. All rights reserved.
 */
#ifndef AMDISA_STRUCTURES_H_
#define AMDISA_STRUCTURES_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <iostream>

namespace amdisa
{
    enum class DataType
    {
        // Data type is unknown or was not set.
        kUnknown,

        kInteger,

        kFloat,

        // Data type to represent unstructured binary data.
        kBits,

        // Data type to represent resource and sampler data.
        kDescriptor
    };

    std::string ToString(const DataType data_type);

    // Constant mapping from JSON base_type to data type enum.
    static const std::map<std::string, DataType> kStringDataTypeToEnum = {{"unknown", DataType::kUnknown},
                                                                          {"integer", DataType::kInteger},
                                                                          {"float", DataType::kFloat},
                                                                          {"bits", DataType::kBits},
                                                                          {"descriptor", DataType::kDescriptor}};

    // Signedness of the field in the data format.
    enum class Signedness
    {
        kUnknown,
        kDoesNotApply,
        kUnsigned,
        kSigned,
        kSignedByModifier
    };

    std::string ToString(const Signedness signedness);

    // Constant mapping from field signedness to strings.
    static const std::map<Signedness, std::string> kSignednessEnumToString = {{Signedness::kUnknown, "Unknown"},
                                                                              {Signedness::kDoesNotApply, "DoesNotApply"},
                                                                              {Signedness::kUnsigned, "Unsigned"},
                                                                              {Signedness::kSigned, "Signed"},
                                                                              {Signedness::kSignedByModifier, "SignedByModifier"}};

    // Generic expression tree. Saves the structure of the tree.
    struct GenericExpressionNode
    {
        std::string expression_operator;

        std::vector<std::shared_ptr<GenericExpressionNode>> children;

        bool is_ret_type_node = false;
    };

    struct SpecMetadata
    {
        std::string copyright;
        std::string sensitivity;
        std::string date;
        std::string schema_version;
    };

    struct Architecture
    {
        std::string name;
        uint32_t id = 0;
    };

    // Padding describes constants values that are inserted into some fields.
    struct Padding
    {
        uint32_t bit_count = 0;
        uint32_t value     = 0;

        operator std::string() const noexcept;
        friend std::ostream& operator<<(std::ostream& os, const Padding& pad);
    };

    // Information describing the range of a Field. The Field can be broken
    // down and spread across the microcode.
    struct Range
    {
        uint32_t order      = 0;
        uint32_t bit_count  = 0;
        uint32_t bit_offset = 0;
        Padding  padding;

        operator std::string() const noexcept;
        friend std::ostream& operator<<(std::ostream& os, const Range& range);
    };

    // Information describing predefined value. A mapping from value to the string name.
    struct PredefinedValue
    {
        std::string name;
        std::string description;
        uint32_t    value = 0;

        operator std::string() const noexcept;
        friend std::ostream& operator<<(std::ostream& os, const PredefinedValue& pv);
    };

    // Information describing a field of a particular section in of the
    // microcode format.
    struct Field
    {
        std::string                  name;
        std::string                  description;
        std::string                  type;
        bool                         is_conditional = false;
        bool                         is_constant    = false;
        uint32_t                     range_count    = 0;
        Signedness                   signedness     = Signedness::kUnknown;
        std::vector<Range>           ranges;
        std::vector<PredefinedValue> predefined_values;

        operator std::string() const noexcept;
        friend std::ostream& operator<<(std::ostream& os, const Field& field);
    };

    // Constant mapping from data type enum to field name string.
    static std::map<DataType, std::string> kDataTypeToFieldName = {{DataType::kDescriptor, "Descriptor"},
                                                                   {DataType::kBits, "Bits"},
                                                                   {DataType::kInteger, "Integer"}};

    // Constant mapping from data type enum to field description string.
    static const std::map<DataType, std::string> kDataTypeToFieldDescription = {{DataType::kDescriptor, "Descriptor field"},
                                                                                {DataType::kBits, "Bits field"},
                                                                                {DataType::kInteger, "Integer field"}};

    // Information describing microcode format, i.e. the structure of the bits
    // in a format.
    struct MicrocodeFormat
    {
        std::vector<Field> bit_map;

        operator std::string() const noexcept;
        friend std::ostream& operator<<(std::ostream& os, const MicrocodeFormat& mf);
    };

    // Encoding conditions.
    struct Condition
    {
        std::string           name;
        uint32_t              id = 0;
        GenericExpressionNode expression;
    };

    // Encoding condition. This structure is used for isolated
    // parsing of encoding conditions.
    struct EncodingCondition
    {
        uint32_t                                  encoding_order = 0;
        uint32_t                                  condition_id = 0;
        std::string                               encoding_name;
        std::string                               condition_name;

        // Map condition IDs to the root of the expression tree that describes the condition.
        std::map<uint32_t, GenericExpressionNode> map_id_to_condition_root;
        MicrocodeFormat                           microcode_format;
    };

    struct Encoding
    {
        std::vector<uint64_t>  identifiers;
        std::vector<Condition> conditions;
        uint64_t               bits        = 0;
        uint64_t               mask        = 0;
        uint64_t               opcode_mask = 0;
        uint64_t               seg_mask    = 0;
        uint32_t               bit_count   = 0;
        int32_t                order       = 0;
        std::string            name;
        std::string            description;
        std::string            category;
        MicrocodeFormat        microcode_format;

        operator std::string() const noexcept;
        friend std::ostream& operator<<(std::ostream& os, const Encoding& enc);
    };

    struct Operand
    {
        std::string field_name;
        std::string data_format;
        std::string type;
        std::string encoding_field_name;
        uint32_t    order           = 0;
        uint32_t    size            = 0;
        bool        input           = false;
        bool        output          = false;
        bool        is_implicit     = false;
        bool        is_in_microcode = false;

        // Some instructions decode the operand
        // as list of registers. For example, BVH.
        bool        is_list         = false;

        operator std::string() const noexcept;
        friend std::ostream& operator<<(std::ostream& os, const Operand& op);
    };

    // Information describing different instruction encodings. The vector of
    // instruction encodings is instantiated in the Instruction struct.
    struct InstructionEncoding
    {
        std::string          name;
        std::string          condition_name;
        uint32_t             condition_id = 0;
        uint32_t             encoding_order = 0;
        uint32_t             opcode = 0;
        std::vector<Operand> operands;

        operator std::string() const noexcept;
        friend std::ostream& operator<<(std::ostream& os, const InstructionEncoding& ie);
    };

    struct Instruction
    {
        std::string                      name;
        std::vector<std::string>         aliased_names;
        std::string                      description;
        std::vector<InstructionEncoding> encodings;

        bool is_branch             = false;
        bool is_conditional_branch = false;
        bool is_indirect_branch    = false;

        // True if this instruction is executed directly by each wave's instruction
        // buffer.
        bool is_immediately_executed = false;

        // True if this instruction indicates the end of the program and false
        // otherwise.
        bool is_program_terminator = false;

        std::string functional_group_name;
        std::vector<std::string> functional_subgroups;

        operator std::string() const noexcept;
        friend std::ostream& operator<<(std::ostream& os, const Instruction& ins);
    };

    struct FunctionalGroupInfo
    {
        std::string name;
        std::string desc;
    };

    struct FunctionalSubgroupInfo
    {
        std::string name;
    };

    // Information describing data attributes. Data attributes are instantiated
    // in DataFormat struct
    struct DataAttributes
    {
        uint32_t           order = 0;
        std::vector<Field> bit_map;

        operator std::string() const noexcept;
        friend std::ostream& operator<<(std::ostream& os, const DataAttributes& da);
    };

    struct DataFormat
    {
        std::string                 name;
        std::string                 description;
        DataType                    data_type       = DataType::kUnknown;
        uint32_t                    bit_count       = 0;
        uint32_t                    component_count = 0;
        std::vector<DataAttributes> data_attributes;

        operator std::string() const noexcept;
        friend std::ostream& operator<<(std::ostream& os, const DataFormat& df);
    };

    struct OperandType
    {
        std::string                  name;
        std::string                  description;
        bool                         is_partitioned = false;
        std::vector<std::string>     subtype_names;
        std::vector<PredefinedValue> predefined_values;
        MicrocodeFormat              microcode_format;

        operator std::string() const noexcept;
        friend std::ostream& operator<<(std::ostream& os, const OperandType& ot);
    };

    // Information related to translation of promoted instructions.
    struct EncodingTranslation
    {
        uint64_t    actual_op_start = 0;
        uint64_t    native_op_start = 0;
        uint64_t    num_ops         = 0;
        std::string actual_encoding;
        std::string native_encoding;
    };

    struct IsaSpec
    {
        SpecMetadata info;

        Architecture architecture;

        std::vector<Encoding> encodings;

        std::vector<Instruction> instructions;

        std::vector<DataFormat> data_formats;

        std::vector<OperandType> operand_types;

        std::vector<FunctionalGroupInfo> functional_group_info;

        std::vector<FunctionalSubgroupInfo> functional_subgroup_info;

        operator std::string() const noexcept;
        friend std::ostream& operator<<(std::ostream& os, const IsaSpec& is);
    };
};      // namespace amdisa
#endif  // AMDISA_STRUCTURES_H_
