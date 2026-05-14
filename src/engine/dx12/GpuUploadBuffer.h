#pragma once

#include "Dx12Utils.h"

template<typename T>
class GpuUploadBuffer
{
public:
    GpuUploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer)
    : mIsConstantBuffer(isConstantBuffer)
    {
        mElementByteSize = sizeof(T);

        if(isConstantBuffer)
            mElementByteSize = Dx12Utils::CalcConstantBufferByteSize(sizeof(T));

        // FIX: no address-of temporaries (required for /permissive-)
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC   bufferDesc =
            CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(mElementByteSize) * elementCount);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&mUploadBuffer)));

        ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));

        // We do not need to unmap until we are done with the resource.
    }

    GpuUploadBuffer(const GpuUploadBuffer& rhs) = delete;
    GpuUploadBuffer& operator=(const GpuUploadBuffer& rhs) = delete;
    ~GpuUploadBuffer()
    {
        if(mUploadBuffer != nullptr)
            mUploadBuffer->Unmap(0, nullptr);

        mMappedData = nullptr;
    }

    ID3D12Resource* Resource()const
    {
        return mUploadBuffer.Get();
    }

    void CopyData(int elementIndex, const T& data)
    {
        memcpy(&mMappedData[elementIndex*mElementByteSize], &data, sizeof(T));
    }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer;
    BYTE* mMappedData = nullptr;

    UINT mElementByteSize = 0;
    bool mIsConstantBuffer = false;
};