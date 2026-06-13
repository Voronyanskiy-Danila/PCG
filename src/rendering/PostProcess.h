#pragma once

#include "PostProcessStructures.h"
#include "../rendering/d3d12/D3d12_GpuUploadBuffer.h"
#include "../rendering/d3d12/d3dx12.h"

#include <d3d12.h>
#include <wrl/client.h>

// Lab 7: scene RT → (vignette → temp) → (chromatic aberration → back buffer)
class PostProcess
{
public:
	static constexpr UINT kSrvCount = 2u;

	void Initialize(ID3D12Device* device, DXGI_FORMAT rtFormat);
	void Resize(ID3D12Device* device, UINT width, UINT height);

	D3D12_CPU_DESCRIPTOR_HANDLE SceneRtv() const;
	void CreateSrvs(
		ID3D12Device* device,
		UINT heapOffsetFirst,
		UINT descriptorIncrementSize,
		ID3D12DescriptorHeap* shaderVisibleSrvHeap);

	void Run(
		ID3D12GraphicsCommandList* cmd,
		ID3D12Resource* backBuffer,
		D3D12_RESOURCE_STATES backBufferStateBefore,
		ID3D12DescriptorHeap* srvHeap,
		UINT srvHeapBaseOffset,
		UINT srvDescriptorIncrement,
		D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,
		const D3D12_VIEWPORT& viewport,
		const D3D12_RECT& scissor,
		bool vignetteEnabled,
		bool chromaticEnabled);

private:
	void TransitionResource(
		ID3D12GraphicsCommandList* cmd,
		ID3D12Resource* res,
		D3D12_RESOURCE_STATES& trackedState,
		D3D12_RESOURCE_STATES newState);
	void BuildPipelines(ID3D12Device* device);
	void CreateTargets(ID3D12Device* device);

	UINT m_width = 0;
	UINT m_height = 0;
	DXGI_FORMAT m_rtFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_sceneColor;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_tempColor;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	UINT m_rtvIncrement = 0;

	D3D12_CPU_DESCRIPTOR_HANDLE m_sceneSrvCpu{};
	D3D12_CPU_DESCRIPTOR_HANDLE m_tempSrvCpu{};

	Microsoft::WRL::ComPtr<ID3DBlob> m_vsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_psVignetteBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_psChromaticBc;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoVignette;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoChromatic;

	std::unique_ptr<GpuUploadBuffer<PostProcessConstants>> m_constantsCb;

	D3D12_RESOURCE_STATES m_sceneState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	D3D12_RESOURCE_STATES m_tempState = D3D12_RESOURCE_STATE_RENDER_TARGET;
};
