#pragma once

#include "../particles/ParticleTypes.h"
#include "../rendering/d3d12/D3d12_GpuUploadBuffer.h"

#include <d3d12.h>
#include <memory>
#include <vector>
#include <wrl/client.h>

struct BillboardGpu
{
	DirectX::XMFLOAT4 Data0{}; // xyz = center, w = half width
	DirectX::XMFLOAT4 Data1{}; // x = half height
};
static_assert(sizeof(BillboardGpu) == 32u, "BillboardGpu must match HLSL");

struct BillboardMaterialConstants
{
	DirectX::XMFLOAT4X4 ViewProj = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	DirectX::XMFLOAT3 CameraRight = {1.0f, 0.0f, 0.0f};
	float _Pad0 = 0.0f;
	DirectX::XMFLOAT3 CameraUp = {0.0f, 1.0f, 0.0f};
	float _Pad1 = 0.0f;
	DirectX::XMFLOAT3 EyePosW = {};
	float _Pad2 = 0.0f;
	DirectX::XMFLOAT3 MatKd = {0.8f, 0.8f, 0.8f};
	float HasDiffuseTexture = 0.0f;
	float MatRoughness = 0.5f;
	float MatMetallic = 0.0f;
	float HasRmTexture = 0.0f;
	float MatNsFallback = 32.0f;
};

class DistantBillboardRenderer
{
public:
	void Initialize(
		ID3D12Device* device,
		ID3D12DescriptorHeap* shaderVisibleSrvHeap,
		UINT instanceSrvHeapIndex,
		UINT srvDescriptorIncrement);
	void DrawGBuffer(
		ID3D12GraphicsCommandList* cmd,
		const std::vector<BillboardGpu>& instances,
		const BillboardMaterialConstants& material,
		ID3D12DescriptorHeap* srvHeap,
		UINT instanceSrvHeapIndex,
		UINT materialSrvBase,
		UINT srvDescriptorIncrement);

private:
	void BuildPipeline(ID3D12Device* device);

	UINT m_maxInstances = 64u;
	UINT m_instanceSrvHeapIndex = 0u;
	UINT m_srvDescriptorIncrement = 0u;
	Microsoft::WRL::ComPtr<ID3DBlob> m_vsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_gsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_psBc;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_instanceBuffer;
	void* m_instanceMapped = nullptr;
	std::unique_ptr<GpuUploadBuffer<BillboardMaterialConstants>> m_materialCb;
};
