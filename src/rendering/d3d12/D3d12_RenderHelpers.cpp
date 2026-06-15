
#include "D3d12_RenderHelpers.h"
#include <comdef.h>

using Microsoft::WRL::ComPtr;

DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber) :
    ErrorCode(hr),
    FunctionName(functionName),
    Filename(filename),
    LineNumber(lineNumber)
{
}

Microsoft::WRL::ComPtr<ID3D12Resource> Dx12Utils::CreateDefaultBuffer(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const void* initData,
    UINT64 byteSize,
    Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer)
{
    using Microsoft::WRL::ComPtr;

    ComPtr<ID3D12Resource> defaultBuffer;

    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC defaultDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);

    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &defaultDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);

    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(uploadBuffer.GetAddressOf())));

    D3D12_SUBRESOURCE_DATA subResourceData = {};
    subResourceData.pData = initData;
    subResourceData.RowPitch = byteSize;
    subResourceData.SlicePitch = subResourceData.RowPitch;

    auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
        defaultBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->ResourceBarrier(1, &toCopyDest);

    UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);

    auto toGenericRead = CD3DX12_RESOURCE_BARRIER::Transition(
        defaultBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->ResourceBarrier(1, &toGenericRead);

    return defaultBuffer;
}

ComPtr<ID3DBlob> Dx12Utils::CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target)
{
	UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	ComPtr<ID3DBlob> byteCode = nullptr;
	ComPtr<ID3DBlob> errors;
	const HRESULT hr = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

    if (errors != nullptr)
    {
        const auto* msg = (char*)errors->GetBufferPointer();
        OutputDebugStringA(msg);
        MessageBoxA(nullptr, msg, "HLSL compile error", MB_OK);
    }

	ThrowIfFailed(hr);

    return byteCode;
}

Microsoft::WRL::ComPtr<ID3D12Resource> Dx12Utils::CreateTexture2DFromRgba(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
	const void* rgba,
	UINT width,
	UINT height,
	Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer)
{
	using Microsoft::WRL::ComPtr;

	ComPtr<ID3D12Resource> texture;
	const UINT64 uploadRowPitch = (static_cast<UINT64>(width) * 4u + 255u) & ~255u;
	const UINT64 uploadSize = uploadRowPitch * static_cast<UINT64>(height);

	CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R8G8B8A8_UNORM,
		width,
		height,
		1u,
		1u);
	ThrowIfFailed(device->CreateCommittedResource(
		&defaultHeap,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(texture.GetAddressOf())));

	CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
	ThrowIfFailed(device->CreateCommittedResource(
		&uploadHeap,
		D3D12_HEAP_FLAG_NONE,
		&uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(uploadBuffer.GetAddressOf())));

	uint8_t* mapped = nullptr;
	ThrowIfFailed(uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped)));
	const auto* src = static_cast<const uint8_t*>(rgba);
	for (UINT y = 0u; y < height; ++y)
	{
		memcpy(
			mapped + uploadRowPitch * y,
			src + static_cast<size_t>(width) * 4u * y,
			static_cast<size_t>(width) * 4u);
	}
	uploadBuffer->Unmap(0, nullptr);

	CD3DX12_TEXTURE_COPY_LOCATION dstLoc{};
	dstLoc.pResource = texture.Get();
	dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dstLoc.SubresourceIndex = 0u;

	CD3DX12_TEXTURE_COPY_LOCATION srcLoc{};
	srcLoc.pResource = uploadBuffer.Get();
	srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	device->GetCopyableFootprints(&texDesc, 0u, 1u, 0u, &srcLoc.PlacedFootprint, nullptr, nullptr, nullptr);

	cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

	const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		texture.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmdList->ResourceBarrier(1u, &barrier);

	return texture;
}

void Dx12Utils::CreateTextureSrv(
	ID3D12Device* device,
	ID3D12Resource* tex,
	D3D12_CPU_DESCRIPTOR_HANDLE dst,
	bool isCubemap)
{
	const D3D12_RESOURCE_DESC desc = tex->GetDesc();
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Format = desc.Format;

	if (isCubemap)
	{
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srv.TextureCube.MipLevels = desc.MipLevels;
		srv.TextureCube.MostDetailedMip = 0;
		srv.TextureCube.ResourceMinLODClamp = 0.f;
	}
	else if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
	{
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
		srv.Texture3D.MipLevels = desc.MipLevels;
		srv.Texture3D.MostDetailedMip = 0;
		srv.Texture3D.ResourceMinLODClamp = 0.f;
	}
	else
	{
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = desc.MipLevels;
		srv.Texture2D.MostDetailedMip = 0;
		srv.Texture2D.ResourceMinLODClamp = 0.f;
	}

	device->CreateShaderResourceView(tex, &srv, dst);
}

std::wstring DxException::ToString() const
{
    _com_error err(ErrorCode);
    std::wstring msg = err.ErrorMessage();

    return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}
