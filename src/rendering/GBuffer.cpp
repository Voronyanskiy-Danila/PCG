#include "GBuffer.h"
#include "../rendering/d3d12/D3d12_RenderHelpers.h"
#include <algorithm>

using Microsoft::WRL::ComPtr;

DXGI_FORMAT GBuffer::RtFormat(UINT i)
{
	static constexpr DXGI_FORMAT kFormats[kRtCount] = {
		DXGI_FORMAT_R8G8B8A8_UNORM,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R8G8B8A8_UNORM,
	};
	return kFormats[(std::min)(i, kRtCount - 1u)];
}

void GBuffer::Destroy()
{
	for (UINT i = 0; i < kRtCount; ++i)
		m_color[i].Reset();
	m_depth.Reset();
	m_rtvHeap.Reset();
	m_dsvHeap.Reset();
	m_width = m_height = 0;
}

void GBuffer::CreateHeaps(ID3D12Device* device)
{
	m_rtv_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_dsv_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	D3D12_DESCRIPTOR_HEAP_DESC dh{};
	dh.NumDescriptors = kRtCount;
	dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	dh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ThrowIfFailed(device->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&m_rtvHeap)));

	dh.NumDescriptors = 1;
	dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	ThrowIfFailed(device->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&m_dsvHeap)));
}

void GBuffer::CreateTextures(ID3D12Device* device)
{
	CD3DX12_HEAP_PROPERTIES heap_default(D3D12_HEAP_TYPE_DEFAULT);

	for (UINT i = 0; i < kRtCount; ++i)
	{
		const DXGI_FORMAT fmt = RtFormat(i);
		CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
			fmt,
			m_width,
			m_height,
			1u,
			1u,
			1u,
			0u,
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

		float clr[4] = { 0.f, 0.f, 0.f, 0.f };
		CD3DX12_CLEAR_VALUE clear_value(fmt, clr);

		ThrowIfFailed(device->CreateCommittedResource(
			&heap_default,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			&clear_value,
			IID_PPV_ARGS(&m_color[i])));
	}

	{
		CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R32_TYPELESS,
			m_width,
			m_height,
			1u,
			1u,
			1u,
			0u,
			D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

		D3D12_CLEAR_VALUE clear_value{};
		clear_value.Format = DXGI_FORMAT_D32_FLOAT;
		clear_value.DepthStencil.Depth = 1.0f;
		clear_value.DepthStencil.Stencil = 0;

		ThrowIfFailed(device->CreateCommittedResource(
			&heap_default,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&clear_value,
			IID_PPV_ARGS(&m_depth)));
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_cpu(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < kRtCount; ++i)
	{
		device->CreateRenderTargetView(m_color[i].Get(), nullptr, rtv_cpu);
		rtv_cpu.Offset(1, m_rtv_increment);
	}

	D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
	dsv.Flags = D3D12_DSV_FLAG_NONE;
	dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsv.Format = DXGI_FORMAT_D32_FLOAT;
	dsv.Texture2D.MipSlice = 0;
	device->CreateDepthStencilView(m_depth.Get(), &dsv, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void GBuffer::Resize(ID3D12Device* device, UINT width, UINT height)
{
	if (width == 0 || height == 0)
		return;

	Destroy();

	m_width = width;
	m_height = height;

	CreateHeaps(device);
	CreateTextures(device);
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::RtvCpu(UINT i) const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
		m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
		static_cast<INT>(i),
		m_rtv_increment);
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::DsvCpu() const
{
	return m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
}