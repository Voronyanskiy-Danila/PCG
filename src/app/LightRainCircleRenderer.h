#pragma once

#include "LightRainSystem.h"
#include "../particles/ParticleTypes.h"
#include "../rendering/d3d12/D3d12_GpuUploadBuffer.h"

#include <d3d12.h>
#include <memory>
#include <wrl/client.h>

class LightRainCircleRenderer
{
public:
	void Initialize(ID3D12Device* device, DXGI_FORMAT rtFormat, UINT maxCircles);
	void Draw(
		ID3D12GraphicsCommandList* cmd,
		const LightRainSystem& rain,
		D3D12_CPU_DESCRIPTOR_HANDLE colorTargetRtv,
		D3D12_CPU_DESCRIPTOR_HANDLE sceneDepthDsv,
		bool useGBufferDepth,
		const D3D12_VIEWPORT& viewport,
		const D3D12_RECT& scissor,
		DirectX::CXMMATRIX view,
		DirectX::CXMMATRIX proj,
		DirectX::FXMVECTOR cameraRight,
		DirectX::FXMVECTOR cameraUp);

	ID3D12DescriptorHeap* SrvHeap() const { return m_srvHeap.Get(); }

private:
	void BuildPipeline(ID3D12Device* device, DXGI_FORMAT rtFormat);

	UINT m_maxCircles = 0u;
	Microsoft::WRL::ComPtr<ID3DBlob> m_vsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_gsBc;
	Microsoft::WRL::ComPtr<ID3DBlob> m_psBc;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoWithDepth;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoNoDepth;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_circleBuffer;
	void* m_circleMapped = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
	UINT m_srvDescriptorSize = 0u;
	std::unique_ptr<GpuUploadBuffer<ParticleDrawConstants>> m_drawCb;
};
