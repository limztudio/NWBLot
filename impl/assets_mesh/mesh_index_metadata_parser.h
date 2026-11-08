// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_metadata.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshIndexMetadataParser final{
private:
    [[nodiscard]] static b8 accumulateLeaves(const Core::Metascript::Value& value, usize& inOutCount)noexcept{
        if(value.isList()){
            for(const Core::Metascript::Value& child : value.asList()){
                if(!accumulateLeaves(child, inOutCount))
                    return false;
            }
            return true;
        }

        if(inOutCount == Limit<usize>::s_Max)
            return false;

        ++inOutCount;
        return true;
    }

    [[nodiscard]] static Expected<usize> countLeaves(const Core::Metascript::Value& value)noexcept{
        usize count = 0u;
        if(!accumulateLeaves(value, count))
            return MakeUnexpected(Failure{});
        return count;
    }

    template<typename IndexVectorT, typename PathArenaT>
    [[nodiscard]] static b8 fillRecursive(
        const ::Path<PathArenaT>& nwbFilePath,
        const Core::Metascript::Value& value,
        const TStringView metaKind,
        const AStringView label,
        Core::Alloc::ScratchArena& scratchArena,
        IndexVectorT& outIndices
    ){
        struct IndexBreadcrumb{
            const IndexBreadcrumb* parent = nullptr;
            usize index = 0u;
        };
        const auto fill = [&](auto&& recurse, const Core::Metascript::Value& current, const IndexBreadcrumb* path) -> b8{
            if(current.isList()){
                const auto& list = current.asList();
                for(usize i = 0u; i < list.size(); ++i){
                    const IndexBreadcrumb child{ path, i };
                    if(!recurse(recurse, list[i], &child))
                        return false;
                }
                return true;
            }

            const auto index = MeshCookMetadata::ValidateMetadataU32Value(current);
            if(!index){
                ScratchString failureLabel{scratchArena};
                const usize maxLabelBytes = failureLabel.max_size();
                usize depth = 0u;
                usize labelBytes = label.size();
                if(labelBytes > maxLabelBytes)
                    throw LengthException("mesh index diagnostic label exceeds string capacity");
                for(const IndexBreadcrumb* component = path; component; component = component->parent){
                    Array<char, s_DecimalTextBufferBytes> digits{};
                    const usize indexBytes = FormatDecimal(component->index, digits).size();
                    if(indexBytes > maxLabelBytes - labelBytes || s_IndexedLabelBracketReserve > maxLabelBytes - labelBytes - indexBytes)
                        throw LengthException("mesh index diagnostic label exceeds string capacity");
                    labelBytes += indexBytes + s_IndexedLabelBracketReserve;
                    ++depth;
                }
                failureLabel.reserve(labelBytes);
                failureLabel.append(label.data(), label.size());
                for(usize remainingDepth = depth; remainingDepth > 0u; --remainingDepth){
                    const IndexBreadcrumb* component = path;
                    for(usize parentCount = 1u; parentCount < remainingDepth; ++parentCount)
                        component = component->parent;
                    Array<char, s_DecimalTextBufferBytes> digits{};
                    const AStringView indexText = FormatDecimal(component->index, digits);
                    failureLabel += '[';
                    failureLabel.append(indexText.data(), indexText.size());
                    failureLabel += ']';
                }
                MeshCookMetadata::LogMetadataU32ValueFailure(nwbFilePath, metaKind, failureLabel, index.error());
                return false;
            }
            using IndexValue = typename IndexVectorT::value_type;
            outIndices.push_back(static_cast<IndexValue>(*index));
            return true;
        };
        return fill(fill, value, nullptr);
    }


public:
    template<typename IndexVectorT, typename PathArenaT>
    [[nodiscard]] static b8 parse(
        const ::Path<PathArenaT>& nwbFilePath,
        const Core::Metascript::Value& asset,
        const TStringView metaKind,
        Core::Alloc::ScratchArena& scratchArena,
        IndexVectorT& outIndices
    ){
        outIndices.clear();
        const auto field = MeshCookMetadata::FindRequiredMetadataListField(nwbFilePath, asset, metaKind, s_MeshIndicesFieldNameView);
        if(!field)
            return false;

        const auto indexCount = countLeaves(**field);
        if(!indexCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' scalar count overflows"), metaKind, PathToString<tchar>(nwbFilePath));
            return false;
        }

        outIndices.reserve(*indexCount);
        if(!fillRecursive(nwbFilePath, **field, metaKind, s_MeshIndicesFieldNameView, scratchArena, outIndices))
            return false;
        NWB_ASSERT(outIndices.size() == *indexCount);
        if(outIndices.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' must not be empty"), metaKind, PathToString<tchar>(nwbFilePath));
            return false;
        }
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
