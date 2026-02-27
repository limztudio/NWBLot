// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Graphics{
public:
    struct BufferSetupDesc{
        BufferDesc bufferDesc;
        const void* data = nullptr;
        usize dataSize = 0;
        u64 destOffsetBytes = 0;
        CommandQueue::Enum queue = CommandQueue::Graphics;
    };

    struct TextureSetupDesc{
        TextureDesc textureDesc;
        const void* data = nullptr;
        // Total upload payload size in bytes (required for async copy ownership).
        usize uploadDataSize = 0;
        usize rowPitch = 0;
        usize depthPitch = 0;
        u32 arraySlice = 0;
        u32 mipLevel = 0;
        CommandQueue::Enum queue = CommandQueue::Graphics;
    };

    struct MeshSetupDesc{
        const void* vertexData = nullptr;
        usize vertexDataSize = 0;
        u32 vertexStride = 0;
        Name vertexBufferName;

        const void* indexData = nullptr;
        usize indexDataSize = 0;
        bool use32BitIndices = true;
        Name indexBufferName;

        CommandQueue::Enum queue = CommandQueue::Graphics;
    };

    struct MeshResource{
        BufferHandle vertexBuffer;
        BufferHandle indexBuffer;
        u32 vertexStride = 0;
        u32 vertexCount = 0;
        u32 indexCount = 0;
        Format::Enum indexFormat = Format::UNKNOWN;

        [[nodiscard]] bool isValid()const noexcept{
            return vertexBuffer != nullptr;
        }
    };

    struct CoopVectorSupport{
        bool inferencingSupported = false;
        bool trainingSupported = false;
        bool fp16InferencingSupported = false;
        bool fp16TrainingSupported = false;
        bool fp32TrainingSupported = false;
    };

    using JobHandle = Alloc::JobSystem::JobHandle;


public:
    Graphics(GraphicsAllocator& allocator, Alloc::ThreadPool& threadPool, Alloc::JobSystem& jobSystem);
    ~Graphics();


public:
    bool init(const Common::FrameData& data);
    bool runFrame();
    void updateWindowState(u32 width, u32 height, bool windowVisible, bool windowIsInFocus);
    void destroy();

public:
    [[nodiscard]] GraphicsAllocator& getAllocator()noexcept{ return m_allocator; }
    [[nodiscard]] IDeviceManager* getDeviceManager()const noexcept{ return m_deviceManager.get(); }
    [[nodiscard]] IDevice* getDevice()const noexcept;

    [[nodiscard]] BufferHandle createBuffer(const BufferDesc& desc)const;
    [[nodiscard]] TextureHandle createTexture(const TextureDesc& desc)const;

    [[nodiscard]] BufferHandle setupBuffer(const BufferSetupDesc& desc)const;
    [[nodiscard]] TextureHandle setupTexture(const TextureSetupDesc& desc)const;
    [[nodiscard]] MeshResource setupMesh(const MeshSetupDesc& desc)const;

    [[nodiscard]] Alloc::JobSystem& getJobSystem()noexcept{ return m_jobSystem; }
    [[nodiscard]] const Alloc::JobSystem& getJobSystem()const noexcept{ return m_jobSystem; }

    [[nodiscard]] JobHandle setupBufferAsync(const BufferSetupDesc& desc, BufferHandle& outBuffer);
    [[nodiscard]] JobHandle setupTextureAsync(const TextureSetupDesc& desc, TextureHandle& outTexture);
    [[nodiscard]] JobHandle setupMeshAsync(const MeshSetupDesc& desc, MeshResource& outMesh);

    [[nodiscard]] CoopVectorSupport queryCoopVecSupport()const;
    [[nodiscard]] CooperativeVectorDeviceFeatures queryCoopVecFeatures()const;
    [[nodiscard]] usize getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, int rows, int columns)const;

    void waitJob(JobHandle handle)const;
    void waitAllJobs()const;


private:
    GraphicsAllocator& m_allocator;
    Alloc::ThreadPool& m_threadPool;
    Alloc::JobSystem& m_jobSystem;
    DeviceCreationParameters m_deviceCreationParams;

private:
    UniquePtr<IDeviceManager> m_deviceManager;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

