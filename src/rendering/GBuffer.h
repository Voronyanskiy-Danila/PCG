#pragma once

#include <array>
#include <d3d12.h>
#include <wrl/client.h>
#include "../rendering/d3d12/d3dx12.h"

class GBuffer
{
public:
	static constexpr UINT kRtCount = 4;

	void Resize(ID3D12Device* device, UINT width, UINT height);

	void Destroy();

	UINT Width() const { return m_width; }
	UINT Height() const { return m_height; }

	ID3D12Resource* ColorTarget(UINT i) const { return m_color[i].Get(); }
	ID3D12Resource* Depth() const { return m_depth.Get(); }

	D3D12_CPU_DESCRIPTOR_HANDLE RtvCpu(UINT i) const;
	D3D12_CPU_DESCRIPTOR_HANDLE DsvCpu() const;

	UINT RtvHeapOffsetCount() const { return kRtCount; }

	static DXGI_FORMAT RtFormat(UINT i);

private:
	void CreateHeaps(ID3D12Device* device);
	void CreateTextures(ID3D12Device* device);

	UINT m_width = 0;
	UINT m_height = 0;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	UINT m_rtv_increment = 0;
	UINT m_dsv_increment = 0;

	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kRtCount> m_color{};
	Microsoft::WRL::ComPtr<ID3D12Resource> m_depth{};
};
