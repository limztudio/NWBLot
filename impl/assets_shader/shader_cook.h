// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../global.h"

#include <core/alloc/scratch.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderCook{
public:
    using CookArena = Core::Alloc::GlobalArena;
    using CookString = AString<CookArena>;

public:
    template<typename T>
    using CookVector = Vector<T, CookArena>;

    template<typename T, typename V>
    using CookMap = HashMap<T, V, Hasher<T>, EqualTo<T>, CookArena>;

    template<typename T>
    using CookHashSet = HashSet<T, Hasher<T>, EqualTo<T>, CookArena>;

    struct ShaderMacroDefinition{
        AStringView name;
        AStringView value;
    };

    struct MaterialBindAttribute{
        CookString name;
        CookVector<CookString> arguments;

        explicit MaterialBindAttribute(CookArena& memoryArena)
            : name(memoryArena)
            , arguments(memoryArena)
        {}
    };

    struct MaterialBindField{
        CookString type;
        CookString name;
        CookVector<MaterialBindAttribute> attributes;

        explicit MaterialBindField(CookArena& memoryArena)
            : type(memoryArena)
            , name(memoryArena)
            , attributes(memoryArena)
        {}

        [[nodiscard]] const MaterialBindAttribute* findAttribute(AStringView attributeName)const;
    };

    struct MaterialBindStruct{
        CookString name;
        CookVector<MaterialBindAttribute> attributes;
        CookVector<MaterialBindField> fields;

        explicit MaterialBindStruct(CookArena& memoryArena)
            : name(memoryArena)
            , attributes(memoryArena)
            , fields(memoryArena)
        {}

        [[nodiscard]] const MaterialBindField* findField(AStringView fieldName)const;
    };

    struct MaterialBindInstance{
        CookString type;
        CookString name;

        explicit MaterialBindInstance(CookArena& memoryArena)
            : type(memoryArena)
            , name(memoryArena)
        {}
    };

    struct MaterialBindEntry{
        CookString source;
        CookString virtualPath;
        CookVector<MaterialBindStruct> structs;
        CookVector<MaterialBindInstance> instances;

        explicit MaterialBindEntry(CookArena& memoryArena)
            : source(memoryArena)
            , virtualPath(memoryArena)
            , structs(memoryArena)
            , instances(memoryArena)
        {}

        void reset(){
            source.clear();
            virtualPath.clear();
            structs.clear();
            instances.clear();
        }

        [[nodiscard]] const MaterialBindStruct* findStruct(AStringView typeName)const;
        [[nodiscard]] const MaterialBindInstance* findInstance(AStringView instanceName)const;
        [[nodiscard]] bool declaresParameter(AStringView parameterName)const;
    };

    struct ShaderCompilerRequest{
        AStringView shaderName;
        AStringView stage;
        AStringView targetProfile;
        AStringView entryPoint;
        AStringView variantName;
        const ShaderMacroDefinition* defines = nullptr;
        u32 defineCount = 0;
        const CookVector<Path>& includeDirectories;
        const Path& sourcePath;
        const Path& outputPath;
    };

    class IShaderCompiler : NoCopy{
    public:
        IShaderCompiler(CookArena& memoryArena)
            : m_memoryArena(memoryArena)
        {}
        virtual ~IShaderCompiler() = default;


    public:
        virtual bool compileVariant(const ShaderCompilerRequest& request, CookVector<u8>& outBytecode) = 0;


    protected:
        CookArena& m_memoryArena;
    };

    using ShaderCompilerFactory = Core::GlobalUniquePtr<IShaderCompiler> (*)(CookArena& memoryArena);

    using DefineCombo = CookMap<CookString, CookString>;

    struct DefineEntry{
        CookVector<CookString> values;

        explicit DefineEntry(CookArena& memoryArena)
            : values(memoryArena)
        {}
        explicit DefineEntry(CookVector<CookString>&& inValues)
            : values(Move(inValues))
        {}
    };


public:
    struct IncludeEntry{
        CookString source;
        CookString defaultVariant;
        CookMap<CookString, DefineEntry> defineValues;

        explicit IncludeEntry(CookArena& memoryArena)
            : source(memoryArena)
            , defaultVariant(memoryArena)
            , defineValues(0, Hasher<CookString>(), EqualTo<CookString>(), memoryArena)
        {}
    };

    struct ShaderEntry{
        CookString name;
        CompactString stage;
        CompactString archiveStage;
        CompactString targetProfile;
        CookString entryPoint;
        CookString source;
        CookString defaultVariant;

        CookVector<CookString> includeRoots;
        CookMap<CookString, DefineEntry> defineValues;
        CookMap<CookString, CookString> implicitDefines;

        explicit ShaderEntry(CookArena& memoryArena)
            : name(memoryArena)
            , entryPoint("main", memoryArena)
            , source(memoryArena)
            , defaultVariant(memoryArena)
            , includeRoots(memoryArena)
            , defineValues(0, Hasher<CookString>(), EqualTo<CookString>(), memoryArena)
            , implicitDefines(0, Hasher<CookString>(), EqualTo<CookString>(), memoryArena)
        {}
    };


private:
    struct SortedDependencyItem{
        CookString canonicalPath;
        Path path;

        explicit SortedDependencyItem(CookArena& arena)
            : canonicalPath(arena)
        {}
    };


public:
    ShaderCook(CookArena& memoryArena, ShaderCompilerFactory compilerFactory = nullptr);


public:
    inline bool compileVariant(const ShaderCompilerRequest& request, CookVector<u8>& outBytecode){ return m_compiler->compileVariant(request, outBytecode); }

public:
    bool parseDocument(const Path& nwbFilePath, Core::Metascript::Document& outDoc);
    bool parseMaterialBindSource(const Path& bindFilePath, MaterialBindEntry& outEntry);
    bool buildMaterialBindIncludeSource(const MaterialBindEntry& entry, CookString& outSource);
    bool parseShaderMeta(const Path& nwbFilePath, const Core::Metascript::Document& doc, ShaderEntry& outEntry);
    bool parseShaderMeta(const Path& nwbFilePath, ShaderEntry& outEntry);
    bool parseIncludeMeta(const Path& nwbFilePath, const Core::Metascript::Document& doc, IncludeEntry& outEntry);
    bool parseIncludeMeta(const Path& nwbFilePath, IncludeEntry& outEntry);

    bool validateDefaultVariant(AStringView contextLabel, AStringView defaultVariant, const CookMap<CookString, DefineEntry>& defineValues);

    void mergeInheritedDefines(ShaderEntry& inOutEntry, const CookVector<Path>& dependencies, const CookMap<CookString, IncludeEntry>& includeMetadata);

    bool gatherShaderDependencies(const Path& sourcePath, const CookVector<Path>& includeDirectories, CookVector<Path>& outDependencies);

    bool expandDefineCombinations(const CookMap<CookString, DefineEntry>& defineValues, CookVector<DefineCombo>& outCombinations);

    CookString buildVariantName(const DefineCombo& combo);
    bool canonicalizeVariantSignature(AStringView variantSignature, CookString& outCanonical);

    bool computeDependencyChecksum(const CookVector<Path>& dependencies, u64& outChecksum);
    bool computeSourceChecksum(const ShaderEntry& entry, const AStringView variantSignature, u64 dependencyChecksum, u64& outChecksum);


private:
    template<typename MapT>
    struct DefineEntryPtr{
        const typename MapT::key_type* key;
        const typename MapT::mapped_type* value;
    };
    template<typename MapT>
    using ScratchDefineEntryVector = Vector<
        DefineEntryPtr<MapT>,
        Core::Alloc::ScratchArena<>
    >;

    template<typename MapT>
    ScratchDefineEntryVector<MapT> sortedDefineEntries(const MapT& map, Core::Alloc::ScratchArena<>& scratchArena){
        using EntryPtr = DefineEntryPtr<MapT>;
        ScratchDefineEntryVector<MapT> entries{scratchArena};
        entries.reserve(map.size());
        for(const auto& [name, value] : map)
            entries.push_back(EntryPtr{ &name, &value });
        Sort(entries.begin(), entries.end(), [](const EntryPtr& lhs, const EntryPtr& rhs){ return *lhs.key < *rhs.key; });
        return entries;
    }


private:
    CookArena& m_memoryArena;

private:
    Core::GlobalUniquePtr<IShaderCompiler> m_compiler;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

