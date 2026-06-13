// =============================================================================
// RenderingSystem.h — G-buffer, компиляция шейдеров, deferred lighting (Lab 2+3)
// =============================================================================
//
// Lab 3: хранит bytecode deferred_tessellation.hlsl (VS, HullHS, DomainDS, PS).
//        Приложение собирает PSO tess из Tess*ByteCode().
// Lab 2: G-buffer, deferred_lighting.hlsl, SRV для lighting pass.
// =============================================================================

#pragma once

#include "CascadedShadowMaps.h"
#include "GBuffer.h"
#include "PostProcess.h"
#include "GpuLightStructures.h"
#include "ShadowStructures.h"
#include "../particles/ParticleTypes.h"
#include "../rendering/d3d12/D3d12_GpuUploadBuffer.h"
#include "../rendering/d3d12/d3dx12.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <memory>
#include <vector>
#include <wrl/client.h>

class RenderingSystem
{
public:
	RenderingSystem() = default;
	RenderingSystem(const RenderingSystem&) = delete;
	RenderingSystem& operator=(const RenderingSystem&) = delete;

	void Initialize(ID3D12Device* device, DXGI_FORMAT backBufferFormat);
	void LoadIblTextures(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
	void ClearIblUploadHeaps();
	void ResizeGBuffer(ID3D12Device* device, UINT width, UINT height);

	void CreateDeferredSrvs(
		ID3D12Device* device,
		UINT heapOffsetFirst,
		UINT descriptorIncrementSize,
		ID3D12DescriptorHeap* shaderVisibleSrvHeap);

	UINT DeferredSrvDescriptorsNeeded() const
	{
		return GBuffer::kRtCount + 2u + kIblSrvCount + PostProcess::kSrvCount;
	}

	static constexpr UINT kIblSrvCount = 3u;

	GBuffer* GetGBuffer() { return &m_gbuffer; }
	const GBuffer* GetGBuffer() const { return &m_gbuffer; }

	// Bytecode Lab 3 — entry points в deferred_tessellation.hlsl
	Microsoft::WRL::ComPtr<ID3DBlob> TessVsByteCode() const { return m_tessVsBc; }
	Microsoft::WRL::ComPtr<ID3DBlob> TessSolidVsByteCode() const { return m_tessSolidVsBc; }
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

	void InitializeShadows(ID3D12Device* device);
	void ResizeShadows(ID3D12Device* device);
	void UpdateShadowCascades(
		DirectX::CXMMATRIX view,
		DirectX::CXMMATRIX proj,
		const DirectX::XMFLOAT3& lightDirWorld,
		const DirectX::XMFLOAT3& eyeWorld,
		const DirectX::XMFLOAT3& cameraForwardWorld,
		const struct Aabb& sceneBounds,
		float cameraNear,
		float cameraFar,
		float cameraFovYRad,
		float cameraAspect);

	void BeginShadowPass(ID3D12GraphicsCommandList* cmd, UINT cascadeIndex);
	void EndShadowPass(ID3D12GraphicsCommandList* cmd);

	void SetShadowPipeline(ID3D12GraphicsCommandList* cmd);
	Microsoft::WRL::ComPtr<ID3D12RootSignature> ShadowRootSignature() const { return m_rsShadow; }
	GpuUploadBuffer<ShadowDrawConstants>& ShadowDrawCb() { return *m_shadowDrawCb; }
	UINT ShadowDrawCbElementSize() const { return m_shadowDrawCbElementSize; }

	const CascadedShadowMaps& Shadows() const { return m_shadows; }

	void TransitionGbufferToRenderTarget(ID3D12GraphicsCommandList* cmd);
	void TransitionGbufferToPixelShader(ID3D12GraphicsCommandList* cmd);

	void SetLightingPipeline(ID3D12GraphicsCommandList* cmd);
	CD3DX12_GPU_DESCRIPTOR_HANDLE LightingSrvGpuStart(
		ID3D12DescriptorHeap* heap,
		UINT srvBaseOffset,
		UINT incr) const;

	GpuUploadBuffer<DeferredLightingConstants>& LightingCb() { return *m_lightingCb; }
	GpuUploadBuffer<ShadowLightingConstants>& ShadowLightingCb() { return *m_shadowLightingCb; }

	void InitializeParticles(ID3D12Device* device, UINT maxParticles);
	void SetParticleEmitter(const DirectX::XMFLOAT3& emitterWorld) { m_particleEmitterPos = emitterWorld; }
	void UpdateParticles(ID3D12GraphicsCommandList* cmd, float deltaTime);
	void DrawParticles(
		ID3D12GraphicsCommandList* cmd,
		D3D12_CPU_DESCRIPTOR_HANDLE colorTargetRtv,
		D3D12_CPU_DESCRIPTOR_HANDLE sceneDepthDsv,
		bool useGBufferDepth,
		const D3D12_VIEWPORT& viewport,
		const D3D12_RECT& scissor,
		DirectX::CXMMATRIX view,
		DirectX::CXMMATRIX proj,
		DirectX::FXMVECTOR cameraRight,
		DirectX::FXMVECTOR cameraUp);

	D3D12_CPU_DESCRIPTOR_HANDLE SceneColorRtv() const { return m_post.SceneRtv(); }
	bool UsesSceneColorTarget() const { return m_postVignetteEnabled || m_postChromaticEnabled; }
	void SetPostVignetteEnabled(bool enabled) { m_postVignetteEnabled = enabled; }
	void SetPostChromaticEnabled(bool enabled) { m_postChromaticEnabled = enabled; }
	bool PostVignetteEnabled() const { return m_postVignetteEnabled; }
	bool PostChromaticEnabled() const { return m_postChromaticEnabled; }

	void RunPostProcess(
		ID3D12GraphicsCommandList* cmd,
		ID3D12Resource* backBuffer,
		D3D12_RESOURCE_STATES backBufferStateBefore,
		ID3D12DescriptorHeap* srvHeap,
		UINT deferredSrvHeapBase,
		UINT srvDescriptorIncrement,
		D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,
		const D3D12_VIEWPORT& viewport,
		const D3D12_RECT& scissor);

private:
	void BuildLightingPipeline(ID3D12Device* device);
	void BuildShadowPipeline(ID3D12Device* device);
	void EnsureLightStructuredBuffer(ID3D12Device* device);
	void BuildParticleComputePipeline(ID3D12Device* device);
	void BuildParticleDrawPipeline(ID3D12Device* device);

	GBuffer m_gbuffer{};
	PostProcess m_post{};
	bool m_postVignetteEnabled = true;
	bool m_postChromaticEnabled = true;

	Microsoft::WRL::ComPtr<ID3DBlob> m_tessVsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_tessSolidVsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_tessHsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_tessDsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_tessPsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_lightVsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_lightPsBc;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rsLighting;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoLighting;

	std::unique_ptr<GpuUploadBuffer<DeferredLightingConstants>> m_lightingCb;
	std::unique_ptr<GpuUploadBuffer<ShadowLightingConstants>> m_shadowLightingCb;
	std::unique_ptr<GpuUploadBuffer<ShadowDrawConstants>> m_shadowDrawCb;
	UINT m_shadowDrawCbElementSize = 0u;

	CascadedShadowMaps m_shadows{};
	Microsoft::WRL::ComPtr<ID3DBlob> m_shadowVsBc;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rsShadow;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoShadow;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_lightGpuBuffer;
	void* m_lightMapped = nullptr;
	UINT m_lightCount = 0;

	bool m_gbIsSrvReadable = false;
	DXGI_FORMAT m_lightingRtFormat = DXGI_FORMAT_UNKNOWN;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_iblIrradiance;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_iblPrefilteredEnv;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_iblIntegrationMap;
	bool m_iblIrradianceIsCubemap = false;
	bool m_iblPrefilterIsCubemap = false;
	bool m_iblIntegrationIsCubemap = false;
	std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_iblUploadHeaps;
	float m_iblMaxEnvMipLevel = 4.0f;

	// Lab 5 particle simulation (Append/Consume ping-pong).
	UINT m_particleMaxCount = 0;
	UINT m_particlePingPong = 0;

	Microsoft::WRL::ComPtr<ID3DBlob> m_particleCsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_particleVsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_particleGsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_particlePsBc;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rsParticleCompute;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rsParticleDraw;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoParticleCompute;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoParticleDraw;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoParticleDrawNoDepth;
	std::unique_ptr<GpuUploadBuffer<ParticleSimConstants>> m_particleSimCb;
	std::unique_ptr<GpuUploadBuffer<ParticleDrawConstants>> m_particleDrawCb;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_particleUavHeap;
	UINT m_particleUavDescriptorSize = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_particleBuffers[2];
	Microsoft::WRL::ComPtr<ID3D12Resource> m_particleCounters[2];
	Microsoft::WRL::ComPtr<ID3D12Resource> m_particleCounterResetZero;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_particleCounterResetMax;
	DirectX::XMFLOAT3 m_particleEmitterPos = {0.0f, 0.5f, 0.0f};
	bool m_particleBufferIsSrv[2] = {false, false};
};
