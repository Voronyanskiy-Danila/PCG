#pragma once

#include "GBuffer.h"
#include "GpuLightStructures.h"
#include "../rendering/d3d12/D3d12_GpuUploadBuffer.h"
#include "../rendering/d3d12/d3dx12.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <memory>
#include <wrl/client.h>

class RenderingSystem
{
public:
	RenderingSystem() = default;
	RenderingSystem(const RenderingSystem&) = delete;
	RenderingSystem& operator=(const RenderingSystem&) = delete;

	void Initialize(ID3D12Device* device, DXGI_FORMAT backBufferFormat);
	void ResizeGBuffer(ID3D12Device* device, UINT width, UINT height);

	void CreateDeferredSrvs(
		ID3D12Device* device,
		UINT heapOffsetFirst,
		UINT descriptorIncrementSize,
		ID3D12DescriptorHeap* shaderVisibleSrvHeap);

	UINT DeferredSrvDescriptorsNeeded() const { return GBuffer::kRtCount + 1u; }

	GBuffer* GetGBuffer() { return &m_gbuffer; }
	const GBuffer* GetGBuffer() const { return &m_gbuffer; }

	Microsoft::WRL::ComPtr<ID3DBlob> GeomVsByteCode() const { return m_geoVsBc; }
	Microsoft::WRL::ComPtr<ID3DBlob> GeomPsByteCode() const { return m_geoPsBc; }

	void SetLights(const GpuLight* lights, UINT count);
	void UpdateLightingFrameConstants(
		ID3D12Device*,
		const DirectX::XMMATRIX& view,
		const DirectX::XMMATRIX& proj,
		const DirectX::XMFLOAT3& eyeWorld);

	void TransitionGbufferToRenderTarget(ID3D12GraphicsCommandList* cmd);
	void TransitionGbufferToPixelShader(ID3D12GraphicsCommandList* cmd);

	ID3D12PipelineState* LightingPSO() const { return m_psoLighting.Get(); }
	ID3D12RootSignature* LightingRootSignature() const { return m_rsLighting.Get(); }

	void SetLightingPipeline(ID3D12GraphicsCommandList* cmd);
	CD3DX12_GPU_DESCRIPTOR_HANDLE LightingSrvGpuStart(
		ID3D12DescriptorHeap* heap,
		UINT srvBaseOffset,
		UINT incr) const;

	GpuUploadBuffer<DeferredLightingConstants>& LightingCb() { return *m_lightingCb; }

private:
	void BuildLightingPipeline(ID3D12Device* device);
	void EnsureLightStructuredBuffer(ID3D12Device* device);

	GBuffer m_gbuffer{};

	Microsoft::WRL::ComPtr<ID3DBlob> m_geoVsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_geoPsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_lightVsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_lightPsBc;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rsLighting;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoLighting;

	std::unique_ptr<GpuUploadBuffer<DeferredLightingConstants>> m_lightingCb;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_lightGpuBuffer;
	void* m_lightMapped = nullptr;
	UINT m_lightCount = 0;

	bool m_gbIsSrvReadable = false;
	DXGI_FORMAT m_lightingRtFormat = DXGI_FORMAT_UNKNOWN;
};
