#include "Importer_Image_DirectXTex.h"

#include "../rendering/d3d12/d3dx12.h"

#include <DirectXTex.h>
#include <algorithm>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

HRESULT LoadTextureImageFromFile12(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
	const wchar_t* filePath,
	ComPtr<ID3D12Resource>& texture,
	ComPtr<ID3D12Resource>& textureUploadHeap,
	bool* isCubemapOut)
{
	if (!device || !cmdList || !filePath)
		return E_INVALIDARG;

	if (isCubemapOut)
		*isCubemapOut = false;

	texture.Reset();
	textureUploadHeap.Reset();

	ScratchImage image;
	TexMetadata metadata{};
	HRESULT hr = LoadFromTGAFile(filePath, TGA_FLAGS_NONE, &metadata, image);
	if (FAILED(hr))
		hr = LoadFromWICFile(filePath, WIC_FLAGS_NONE, &metadata, image);
	if (FAILED(hr))
		hr = LoadFromDDSFile(filePath, DDS_FLAGS_NONE, &metadata, image);
	if (FAILED(hr))
		return hr;

	if (isCubemapOut)
		*isCubemapOut = metadata.IsCubemap() != 0;

	D3D12_RESOURCE_DESC texDesc{};
	if (metadata.dimension == TEX_DIMENSION_TEXTURE3D)
		texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	else
		texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = metadata.width;
	texDesc.Height = static_cast<UINT>(metadata.height);
	texDesc.DepthOrArraySize = static_cast<UINT16>(
		metadata.dimension == TEX_DIMENSION_TEXTURE3D ? metadata.depth : metadata.arraySize);
	texDesc.MipLevels = static_cast<UINT16>(metadata.mipLevels);
	texDesc.Format = metadata.format;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
	hr = device->CreateCommittedResource(
		&defaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&texture));
	if (FAILED(hr))
		return hr;

	const UINT subresourceCount = static_cast<UINT>(metadata.mipLevels) *
		static_cast<UINT>(metadata.dimension == TEX_DIMENSION_TEXTURE3D ? metadata.depth : metadata.arraySize);
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, subresourceCount);

	CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

	hr = device->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&textureUploadHeap));
	if (FAILED(hr))
	{
		texture.Reset();
		return hr;
	}

	std::vector<D3D12_SUBRESOURCE_DATA> subresources(image.GetImageCount());
	for (size_t i = 0; i < image.GetImageCount(); ++i)
	{
		const Image* img = image.GetImages() + i;
		subresources[i].pData = img->pixels;
		subresources[i].RowPitch = static_cast<UINT>(img->rowPitch);
		subresources[i].SlicePitch = static_cast<UINT>(img->slicePitch);
	}

	{
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			texture.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_COPY_DEST);
		cmdList->ResourceBarrier(1, &barrier);
	}

	UpdateSubresources(cmdList, texture.Get(), textureUploadHeap.Get(), 0, 0, subresourceCount, subresources.data());

	{
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			texture.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmdList->ResourceBarrier(1, &barrier);
	}

	return S_OK;
}
