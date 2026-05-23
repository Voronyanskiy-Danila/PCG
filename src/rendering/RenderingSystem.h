// =============================================================================
// RenderingSystem.h — G-buffer, компиляция шейдеров, deferred lighting (Lab 2+3)
// =============================================================================
//
// Lab 3: хранит bytecode deferred_tessellation.hlsl (VS, HullHS, DomainDS, PS).
//        Приложение собирает PSO tess из Tess*ByteCode().
// Lab 2: G-buffer, deferred_lighting.hlsl, SRV для lighting pass.
// =============================================================================

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

	// Bytecode Lab 3 — entry points в deferred_tessellation.hlsl
	Microsoft::WRL::ComPtr<ID3DBlob> TessVsByteCode() const { return m_tessVsBc; }
	Microsoft::WRL::ComPtr<ID3DBlob> TessHsByteCode() const { return m_tessHsBc; }
	Microsoft::WRL::ComPtr<ID3DBlob> TessDsByteCode() const { return m_tessDsBc; }
	Microsoft::WRL::ComPtr<ID3DBlob> TessPsByteCode() const { return m_tessPsBc; }

	void SetLights(const GpuLight* lights, UINT count);
	void UpdateLightingFrameConstants(
		ID3D12Device*,
		const DirectX::XMFLOAT3& eyeWorld,
		const DirectX::XMFLOAT3& dirLightWorld,
		const DirectX::XMFLOAT3& dirLightColor,
		float dirIntensity);

	void TransitionGbufferToRenderTarget(ID3D12GraphicsCommandList* cmd);
	void TransitionGbufferToPixelShader(ID3D12GraphicsCommandList* cmd);

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

	Microsoft::WRL::ComPtr<ID3DBlob> m_tessVsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_tessHsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_tessDsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_tessPsBc;
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
