#include "TextureLoaderDirectXTex.h"

#include "../engine/dx12/Dx12Core.h"

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
	ComPtr<ID3D12Resource>& textureUploadHeap)
{
	if (!device || !cmdList || !filePath)
		return E_INVALIDARG;

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

	D3D12_RESOURCE_DESC texDesc{};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = metadata.width;
	texDesc.Height = static_cast<UINT>(metadata.height);
	texDesc.DepthOrArraySize = static_cast<UINT16>(metadata.arraySize);
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

	const UINT numSubresources = texDesc.DepthOrArraySize * texDesc.MipLevels;
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, numSubresources);

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

	UpdateSubresources(cmdList, texture.Get(), textureUploadHeap.Get(), 0, 0, numSubresources, subresources.data());

	{
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			texture.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmdList->ResourceBarrier(1, &barrier);
	}

	return S_OK;
}
