#include "RenderingSystem.h"
#include "../rendering/d3d12/D3d12_RenderHelpers.h"
#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace
{

void CreateTextureSrv(ID3D12Device* device, ID3D12Resource* tex, DXGI_FORMAT fmt, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC d{};
	d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	d.Format = fmt;
	d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	d.Texture2D.MipLevels = 1;
	d.Texture2D.MostDetailedMip = 0;
	d.Texture2D.ResourceMinLODClamp = 0.f;
	device->CreateShaderResourceView(tex, &d, dst);
}

} // namespace

void RenderingSystem::EnsureLightStructuredBuffer(ID3D12Device* device)
{
	if (m_lightGpuBuffer)
		return;

	const UINT byteSize = kDeferredMaxLights * kGpuLightStride;
	CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(byteSize));

	ThrowIfFailed(device->CreateCommittedResource(
		&heap,
		D3D12_HEAP_FLAG_NONE,
		&rd,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_lightGpuBuffer)));
	ThrowIfFailed(m_lightGpuBuffer->Map(0, nullptr, &m_lightMapped));
	std::memset(m_lightMapped, 0, byteSize);
}

void RenderingSystem::Initialize(ID3D12Device* device, DXGI_FORMAT backBufferFormat)
{
	m_lightingRtFormat = backBufferFormat;

	m_geoVsBc = Dx12Utils::CompileShader(
		L"content/shaders/deferred_gbuffer.hlsl", nullptr, "VS", "vs_5_0");
	m_geoPsBc = Dx12Utils::CompileShader(
		L"content/shaders/deferred_gbuffer.hlsl", nullptr, "PS", "ps_5_0");

	m_lightVsBc = Dx12Utils::CompileShader(
		L"content/shaders/deferred_lighting.hlsl", nullptr, "VS_Light", "vs_5_0");
	m_lightPsBc = Dx12Utils::CompileShader(
		L"content/shaders/deferred_lighting.hlsl", nullptr, "PS_Light", "ps_5_0");

	m_lightingCb = std::make_unique<GpuUploadBuffer<DeferredLightingConstants>>(device, 1u, true);
	EnsureLightStructuredBuffer(device);
	BuildLightingPipeline(device);
}

void RenderingSystem::BuildLightingPipeline(ID3D12Device* device)
{
	CD3DX12_DESCRIPTOR_RANGE srvRange{};
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5u, 0u);

	CD3DX12_ROOT_PARAMETER rp[2]{};
	rp[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
	rp[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_STATIC_SAMPLER_DESC s0(
		0,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		0.f,
		0,
		D3D12_COMPARISON_FUNC_NEVER,
		D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
		0.f,
		D3D12_FLOAT32_MAX,
		D3D12_SHADER_VISIBILITY_PIXEL,
		0);

	CD3DX12_ROOT_SIGNATURE_DESC rsd{};
	rsd.NumParameters = 2;
	rsd.pParameters = rp;
	rsd.NumStaticSamplers = 1;
	rsd.pStaticSamplers = &s0;
	rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> serialized;
	ComPtr<ID3DBlob> errors;
	ThrowIfFailed(D3D12SerializeRootSignature(
		&rsd,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serialized.GetAddressOf(),
		errors.GetAddressOf()));
	if (errors)
		OutputDebugStringA(static_cast<char*>(errors->GetBufferPointer()));
	ThrowIfFailed(device->CreateRootSignature(
		0,
		serialized->GetBufferPointer(),
		serialized->GetBufferSize(),
		IID_PPV_ARGS(&m_rsLighting)));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.InputLayout.NumElements = 0;
	pso.InputLayout.pInputElementDescs = nullptr;
	pso.pRootSignature = m_rsLighting.Get();
	pso.VS = {m_lightVsBc->GetBufferPointer(), m_lightVsBc->GetBufferSize()};
	pso.PS = {m_lightPsBc->GetBufferPointer(), m_lightPsBc->GetBufferSize()};

	CD3DX12_RASTERIZER_DESC rs(D3D12_DEFAULT);
	rs.CullMode = D3D12_CULL_MODE_NONE;
	pso.RasterizerState = rs;
	pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	CD3DX12_DEPTH_STENCIL_DESC ds(D3D12_DEFAULT);
	ds.DepthEnable = FALSE;
	ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	pso.DepthStencilState = ds;
	pso.SampleMask = UINT_MAX;
	pso.SampleDesc.Count = 1;
	pso.SampleDesc.Quality = 0;
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.NumRenderTargets = 1;
	pso.RTVFormats[0] = m_lightingRtFormat;
	pso.DSVFormat = DXGI_FORMAT_UNKNOWN;

	ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoLighting)));
}

void RenderingSystem::ResizeGBuffer(ID3D12Device* device, UINT width, UINT height)
{
	m_gbuffer.Resize(device, width, height);
	m_gbIsSrvReadable = false;
}

void RenderingSystem::CreateDeferredSrvs(
	ID3D12Device* device,
	UINT heapOffsetFirst,
	UINT descriptorIncrementSize,
	ID3D12DescriptorHeap* shaderVisibleSrvHeap)
{
	D3D12_CPU_DESCRIPTOR_HANDLE h = shaderVisibleSrvHeap->GetCPUDescriptorHandleForHeapStart();

	for (UINT i = 0; i < GBuffer::kRtCount; ++i)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE slot = h;
		slot.ptr += static_cast<SIZE_T>(heapOffsetFirst + i) * descriptorIncrementSize;
		CreateTextureSrv(device, m_gbuffer.ColorTarget(i), GBuffer::RtFormat(i), slot);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE lslot = h;
	lslot.ptr += static_cast<SIZE_T>(heapOffsetFirst + GBuffer::kRtCount) * descriptorIncrementSize;

	D3D12_SHADER_RESOURCE_VIEW_DESC sbv{};
	sbv.Format = DXGI_FORMAT_UNKNOWN;
	sbv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sbv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	sbv.Buffer.FirstElement = 0;
	sbv.Buffer.NumElements = kDeferredMaxLights;
	sbv.Buffer.StructureByteStride = kGpuLightStride;
	sbv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	device->CreateShaderResourceView(m_lightGpuBuffer.Get(), &sbv, lslot);
}

void RenderingSystem::SetLights(const GpuLight* lights, UINT count)
{
	if (!lights || count == 0 || !m_lightMapped)
	{
		m_lightCount = 0;
		return;
	}
	count = (std::min)(count, kDeferredMaxLights);
	std::memcpy(m_lightMapped, lights, sizeof(GpuLight) * count);
	m_lightCount = count;
	if (count < kDeferredMaxLights)
		std::memset(reinterpret_cast<BYTE*>(m_lightMapped) + sizeof(GpuLight) * count,
			0,
			sizeof(GpuLight) * (kDeferredMaxLights - count));
}

void RenderingSystem::UpdateLightingFrameConstants(
	ID3D12Device*,
	const XMFLOAT3& eyeWorld,
	const XMFLOAT3& dirLightWorld,
	const XMFLOAT3& dirLightColor,
	float dirIntensity)
{
	DeferredLightingConstants c{};
	c.EyeWorld = XMFLOAT4(eyeWorld.x, eyeWorld.y, eyeWorld.z, 1.f);

	XMFLOAT3 dirN{};
	XMVECTOR dv = XMVector3Normalize(XMLoadFloat3(&dirLightWorld));
	XMStoreFloat3(&dirN, dv);
	c.DirDirection = XMFLOAT4(dirN.x, dirN.y, dirN.z, 0.f);

	c.DirColorIntensity = XMFLOAT4(dirLightColor.x, dirLightColor.y, dirLightColor.z, dirIntensity);

	c.NumLights = m_lightCount;
	c._Pad0 = c._Pad1 = c._Pad2 = 0;
	m_lightingCb->CopyData(0, c);
}

void RenderingSystem::TransitionGbufferToPixelShader(ID3D12GraphicsCommandList* cmd)
{
	for (UINT i = 0; i < GBuffer::kRtCount; ++i)
	{
		auto bar = CD3DX12_RESOURCE_BARRIER::Transition(
			m_gbuffer.ColorTarget(i),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &bar);
	}
	m_gbIsSrvReadable = true;
}

void RenderingSystem::TransitionGbufferToRenderTarget(ID3D12GraphicsCommandList* cmd)
{
	if (!m_gbIsSrvReadable)
		return;

	for (UINT i = 0; i < GBuffer::kRtCount; ++i)
	{
		auto bar = CD3DX12_RESOURCE_BARRIER::Transition(
			m_gbuffer.ColorTarget(i),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_RENDER_TARGET);
		cmd->ResourceBarrier(1, &bar);
	}
	m_gbIsSrvReadable = false;
}

void RenderingSystem::SetLightingPipeline(ID3D12GraphicsCommandList* cmd)
{
	cmd->SetGraphicsRootSignature(m_rsLighting.Get());
	cmd->SetPipelineState(m_psoLighting.Get());
}

CD3DX12_GPU_DESCRIPTOR_HANDLE RenderingSystem::LightingSrvGpuStart(
	ID3D12DescriptorHeap* heap,
	UINT srvBaseOffset,
	UINT incr) const
{
	return CD3DX12_GPU_DESCRIPTOR_HANDLE(
		heap->GetGPUDescriptorHandleForHeapStart(),
		static_cast<INT>(srvBaseOffset),
		incr);
}
