// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compute_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Every work buffer is selected from the global StorageBuffer heap through the shared target-generation slot
// payload. Keep the established names below so the warp algorithm remains readable.
#define g_CoverageWords nwbAvboitCoverageWords()
#define g_DepthWarp nwbAvboitDepthWarpUav()
#define g_Control nwbAvboitControlUav()


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbAvboitCoverageWord(uint wordIndex){
    const uint validSliceCount = min(NWB_AVBOIT_COVERAGE_SLICES_PER_WORD, nwbAvboitVirtualSliceCount() - (wordIndex << NWB_AVBOIT_COVERAGE_WORD_SHIFT));
    return g_CoverageWords[wordIndex] & (~0u >> (NWB_AVBOIT_COVERAGE_SLICES_PER_WORD - validSliceCount));
}

uint nwbAvboitCoarsenCoverageWord(uint coverageWord, uint compressionShift){
    // Each reduction keeps one low-bit marker per occupied aligned bin.
    if(compressionShift > 0u)
        coverageWord = (coverageWord | (coverageWord >> 1u)) & 0x55555555u;
    if(compressionShift > 1u)
        coverageWord = (coverageWord | (coverageWord >> 2u)) & 0x11111111u;
    if(compressionShift > 2u)
        coverageWord = (coverageWord | (coverageWord >> 4u)) & 0x01010101u;
    if(compressionShift > 3u)
        coverageWord = (coverageWord | (coverageWord >> 8u)) & 0x00010001u;
    if(compressionShift > 4u)
        coverageWord = (coverageWord | (coverageWord >> 16u)) & 0x00000001u;
    return coverageWord;
}

uint nwbAvboitCoarseBinCount(uint compressionShift){
    const uint virtualSliceCount = nwbAvboitVirtualSliceCount();
    return (virtualSliceCount >> compressionShift) + uint((virtualSliceCount & ((1u << compressionShift) - 1u)) != 0u);
}

bool nwbAvboitCoarseBinOccupied(
    uint coarseBin,
    uint compressionShift,
    inout uint cachedWordIndex,
    inout uint cachedCoverageWord){
    const uint beginSlice = coarseBin << compressionShift;
    if(compressionShift <= NWB_AVBOIT_COVERAGE_WORD_SHIFT){
        const uint wordIndex = beginSlice >> NWB_AVBOIT_COVERAGE_WORD_SHIFT;
        if(wordIndex != cachedWordIndex){
            cachedWordIndex = wordIndex;
            cachedCoverageWord = nwbAvboitCoarsenCoverageWord(nwbAvboitCoverageWord(wordIndex), compressionShift);
        }
        return (cachedCoverageWord & (1u << (beginSlice & NWB_AVBOIT_COVERAGE_BIT_MASK))) != 0u;
    }

    const uint endSlice = beginSlice + min(1u << compressionShift, nwbAvboitVirtualSliceCount() - beginSlice);
    const uint beginWord = beginSlice >> NWB_AVBOIT_COVERAGE_WORD_SHIFT;
    const uint endWord = ((endSlice - 1u) >> NWB_AVBOIT_COVERAGE_WORD_SHIFT) + 1u;
    for(uint wordIndex = beginWord; wordIndex < endWord; ++wordIndex){
        if(nwbAvboitCoverageWord(wordIndex) != 0u)
            return true;
    }
    return false;
}

uint nwbAvboitCountOccupiedBins(uint compressionShift){
    uint occupiedCount = 0u;
    if(compressionShift <= NWB_AVBOIT_COVERAGE_WORD_SHIFT){
        const uint virtualSliceCount = nwbAvboitVirtualSliceCount();
        const uint wordCount = (virtualSliceCount >> NWB_AVBOIT_COVERAGE_WORD_SHIFT) + uint((virtualSliceCount & NWB_AVBOIT_COVERAGE_BIT_MASK) != 0u);
        for(uint wordIndex = 0u; wordIndex < wordCount; ++wordIndex)
            occupiedCount += countbits(nwbAvboitCoarsenCoverageWord(nwbAvboitCoverageWord(wordIndex), compressionShift));
    }
    else{
        const uint binCount = nwbAvboitCoarseBinCount(compressionShift);
        uint cachedWordIndex = NWB_AVBOIT_WARP_INVALID;
        uint cachedCoverageWord = 0u;
        for(uint bin = 0u; bin < binCount; ++bin){
            if(nwbAvboitCoarseBinOccupied(bin, compressionShift, cachedWordIndex, cachedCoverageWord))
                ++occupiedCount;
        }
    }
    return occupiedCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbAvboitDepthWarpMain(){
    uint compressionShift = 0u;
    uint occupiedCount = nwbAvboitCountOccupiedBins(compressionShift);
    while(occupiedCount > nwbAvboitPhysicalSliceCount() && ((1u << compressionShift) < nwbAvboitVirtualSliceCount())){
        ++compressionShift;
        occupiedCount = nwbAvboitCountOccupiedBins(compressionShift);
    }

    const uint binCount = nwbAvboitCoarseBinCount(compressionShift);
    uint physicalIndex = 0u;
    uint previousPhysicalIndex = 0u;
    uint cachedWordIndex = NWB_AVBOIT_WARP_INVALID;
    uint cachedCoverageWord = 0u;

    for(uint bin = 0u; bin < binCount; ++bin){
        const bool occupied = nwbAvboitCoarseBinOccupied(bin, compressionShift, cachedWordIndex, cachedCoverageWord);
        const uint mappedIndex = occupied ? min(physicalIndex, nwbAvboitPhysicalSliceCount() - 1u) : previousPhysicalIndex;
        const uint beginSlice = bin << compressionShift;
        const uint endSlice = beginSlice + min(1u << compressionShift, nwbAvboitVirtualSliceCount() - beginSlice);

        for(uint virtualSlice = beginSlice; virtualSlice < endSlice; ++virtualSlice)
            g_DepthWarp[virtualSlice] = mappedIndex;

        if(occupied){
            previousPhysicalIndex = mappedIndex;
            ++physicalIndex;
        }
    }

    g_Control[NWB_AVBOIT_CONTROL_ACTIVE_PHYSICAL_SLICES] = min(max(occupiedCount, 1u), nwbAvboitPhysicalSliceCount());
    g_Control[NWB_AVBOIT_CONTROL_COMPRESSION_SHIFT] = compressionShift;
    g_Control[NWB_AVBOIT_CONTROL_VIRTUAL_SLICE_COUNT] = nwbAvboitVirtualSliceCount();
    g_Control[NWB_AVBOIT_CONTROL_PHYSICAL_SLICE_COUNT] = nwbAvboitPhysicalSliceCount();
    g_Control[NWB_AVBOIT_CONTROL_LOW_WIDTH] = nwbAvboitLowWidth();
    g_Control[NWB_AVBOIT_CONTROL_LOW_HEIGHT] = nwbAvboitLowHeight();
    g_Control[NWB_AVBOIT_CONTROL_FULL_WIDTH] = nwbAvboitFullWidth();
    g_Control[NWB_AVBOIT_CONTROL_FULL_HEIGHT] = nwbAvboitFullHeight();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_AVBOIT_DEPTH_WARP_GROUP_SIZE_X, 1, 1)]
void main(){
    nwbAvboitDepthWarpMain();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef g_CoverageWords
#undef g_DepthWarp
#undef g_Control


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

