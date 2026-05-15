#pragma once

#include <d3d12.h>
#include <wrl/client.h>

// Загрузка в GPU-текстуру D3D12 по той же схеме, что в kg26-27 Lab1 (Crate):
// TGA → WIC (PNG/JPG/…) → DDS (см. DirectXTex).
HRESULT LoadTextureImageFromFile12(
	_In_ ID3D12Device* device,
	_In_ ID3D12GraphicsCommandList* cmdList,
	_In_z_ const wchar_t* filePath,
	_Out_ Microsoft::WRL::ComPtr<ID3D12Resource>& texture,
	_Out_ Microsoft::WRL::ComPtr<ID3D12Resource>& textureUploadHeap);
