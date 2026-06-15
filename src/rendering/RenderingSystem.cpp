// =============================================================================
// RenderingSystem.cpp
// =============================================================================
// Initialize: компилирует deferred_tessellation.hlsl (4 профиля) и
//             deferred_lighting.hlsl (2 профиля).
// CreateDeferredSrvs: SRV на RT G-buffer + structured buffer огней для PS_Light.
// =============================================================================

#include "RenderingSystem.h"
#include "../importers/Importer_Image_DirectXTex.h"
#include "../rendering/d3d12/D3d12_RenderHelpers.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

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

void ResetCounter(
	ID3D12GraphicsCommandList* cmd,
	ID3D12Resource* counter,
	ID3D12Resource* srcUpload)
{
	auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
		counter,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COPY_DEST);
	cmd->ResourceBarrier(1u, &toCopy);
	cmd->CopyBufferRegion(counter, 0u, srcUpload, 0u, 4u);
	auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(
		counter,
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmd->ResourceBarrier(1u, &toUav);
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

	// Lab 3 — геометрия: tessellation + displacement + G-buffer MRT
	m_tessVsBc = Dx12Utils::CompileShader(
		L"content/shaders/deferred_tessellation.hlsl", nullptr, "VS", "vs_5_0");
	m_tessSolidVsBc = Dx12Utils::CompileShader(
		L"content/shaders/deferred_tessellation.hlsl", nullptr, "VS_GBuffer", "vs_5_0");
	m_tessHsBc = Dx12Utils::CompileShader(
		L"content/shaders/deferred_tessellation.hlsl", nullptr, "HullHS", "hs_5_0");
	m_tessDsBc = Dx12Utils::CompileShader(
		L"content/shaders/deferred_tessellation.hlsl", nullptr, "DomainDS", "ds_5_0");
	m_tessPsBc = Dx12Utils::CompileShader(
		L"content/shaders/deferred_tessellation.hlsl", nullptr, "PS", "ps_5_0");


	m_lightVsBc = Dx12Utils::CompileShader(
		L"content/shaders/deferred_lighting.hlsl", nullptr, "VS_Light", "vs_5_0");
	m_lightPsBc = Dx12Utils::CompileShader(
		L"content/shaders/deferred_lighting.hlsl", nullptr, "PS_Light", "ps_5_0");

	m_lightingCb = std::make_unique<GpuUploadBuffer<DeferredLightingConstants>>(device, 1u, true);
	m_shadowLightingCb = std::make_unique<GpuUploadBuffer<ShadowLightingConstants>>(device, 1u, true);
	m_shadowDrawCb =
		std::make_unique<GpuUploadBuffer<ShadowDrawConstants>>(device, kMaxShadowDrawCalls, true);
	m_shadowDrawCbElementSize =
		Dx12Utils::CalcConstantBufferByteSize(sizeof(ShadowDrawConstants));
	EnsureLightStructuredBuffer(device);
	InitializeShadows(device);
	BuildLightingPipeline(device);
	BuildShadowPipeline(device);
	m_post.Initialize(device, backBufferFormat);
}

void RenderingSystem::LoadIblTextures(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
	if (!device || !cmdList)
	{
		throw DxException(
			E_INVALIDARG,
			L"LoadIblTextures requires valid device and command list.",
			AnsiToWString(__FILE__),
			__LINE__);
	}

	auto loadOne = [&](const wchar_t* path,
					   Microsoft::WRL::ComPtr<ID3D12Resource>& out,
					   bool& outIsCubemap) {
		Microsoft::WRL::ComPtr<ID3D12Resource> upload;
		const HRESULT hr =
			LoadTextureImageFromFile12(device, cmdList, path, out, upload, &outIsCubemap);
		if (FAILED(hr))
		{
			std::wstring msg = L"Failed to load IBL texture:\n";
			msg += path;
			msg += L"\n\nExpected under project root: Stuff/*.dds";
			throw DxException(hr, msg, AnsiToWString(__FILE__), __LINE__);
		}
		m_iblUploadHeaps.push_back(upload);
	};

	loadOne(L"Stuff/IrradianceMap_BC6U.dds", m_iblIrradiance, m_iblIrradianceIsCubemap);
	loadOne(L"Stuff/PreFilteredEnvMap_BC6U.dds", m_iblPrefilteredEnv, m_iblPrefilterIsCubemap);
	loadOne(L"Stuff/IntegrationMap.dds", m_iblIntegrationMap, m_iblIntegrationIsCubemap);

	if (m_iblPrefilteredEnv)
	{
		const D3D12_RESOURCE_DESC preDesc = m_iblPrefilteredEnv->GetDesc();
		m_iblMaxEnvMipLevel = static_cast<float>((std::max)(static_cast<UINT>(preDesc.MipLevels), 1u) - 1u);
	}
}

void RenderingSystem::ClearIblUploadHeaps()
{
	m_iblUploadHeaps.clear();
}

void RenderingSystem::BuildParticleComputePipeline(ID3D12Device* device)
{
	CD3DX12_DESCRIPTOR_RANGE uavRange{};
	uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2u, 0u);

	CD3DX12_ROOT_PARAMETER rp[2]{};
	rp[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
	rp[1].InitAsDescriptorTable(1, &uavRange, D3D12_SHADER_VISIBILITY_ALL);

	CD3DX12_ROOT_SIGNATURE_DESC rsd{};
	rsd.Init(
		static_cast<UINT>(_countof(rp)),
		rp,
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_NONE);

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
		IID_PPV_ARGS(&m_rsParticleCompute)));

	D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
	pso.pRootSignature = m_rsParticleCompute.Get();
	pso.CS = {m_particleCsBc->GetBufferPointer(), m_particleCsBc->GetBufferSize()};
	ThrowIfFailed(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&m_psoParticleCompute)));
}

void RenderingSystem::BuildParticleDrawPipeline(ID3D12Device* device)
{
	CD3DX12_DESCRIPTOR_RANGE srvRange{};
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 0u);

	CD3DX12_ROOT_PARAMETER rp[2]{};
	rp[0].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL);
	rp[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);

	CD3DX12_ROOT_SIGNATURE_DESC rsd{};
	rsd.Init(
		static_cast<UINT>(_countof(rp)),
		rp,
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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
		IID_PPV_ARGS(&m_rsParticleDraw)));

	auto makeParticleBlendDesc = []() {
		CD3DX12_BLEND_DESC blend(D3D12_DEFAULT);
		auto& rt = blend.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.LogicOpEnable = FALSE;
		rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rt.DestBlend = D3D12_BLEND_ONE;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.SrcBlendAlpha = D3D12_BLEND_ONE;
		rt.DestBlendAlpha = D3D12_BLEND_ONE;
		rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		return blend;
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.InputLayout = {nullptr, 0u};
	pso.pRootSignature = m_rsParticleDraw.Get();
	pso.VS = {m_particleVsBc->GetBufferPointer(), m_particleVsBc->GetBufferSize()};
	pso.GS = {m_particleGsBc->GetBufferPointer(), m_particleGsBc->GetBufferSize()};
	pso.PS = {m_particlePsBc->GetBufferPointer(), m_particlePsBc->GetBufferSize()};
	CD3DX12_RASTERIZER_DESC rs(D3D12_DEFAULT);
	rs.CullMode = D3D12_CULL_MODE_NONE;
	pso.RasterizerState = rs;
	pso.BlendState = makeParticleBlendDesc();
	CD3DX12_DEPTH_STENCIL_DESC ds(D3D12_DEFAULT);
	ds.DepthEnable = TRUE;
	ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	pso.DepthStencilState = ds;
	pso.SampleMask = UINT_MAX;
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	pso.NumRenderTargets = 1u;
	pso.RTVFormats[0] = m_lightingRtFormat;
	pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pso.SampleDesc.Count = 1u;
	pso.SampleDesc.Quality = 0u;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoParticleDraw)));

	CD3DX12_DEPTH_STENCIL_DESC dsNoDepth(D3D12_DEFAULT);
	dsNoDepth.DepthEnable = FALSE;
	dsNoDepth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	pso.DepthStencilState = dsNoDepth;
	pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoParticleDrawNoDepth)));
}

void RenderingSystem::InitializeParticles(ID3D12Device* device, UINT maxParticles)
{
	m_particleMaxCount = maxParticles;
	m_particlePingPong = 0u;
	if (m_particleMaxCount == 0u)
		return;

	m_particleCsBc = Dx12Utils::CompileShader(
		L"content/shaders/particles.hlsl", nullptr, "CS_Update", "cs_5_0");
	m_particleVsBc = Dx12Utils::CompileShader(
		L"content/shaders/particles.hlsl", nullptr, "VS_Particle", "vs_5_0");
	m_particleGsBc = Dx12Utils::CompileShader(
		L"content/shaders/particles.hlsl", nullptr, "GS_Billboard", "gs_5_0");
	m_particlePsBc = Dx12Utils::CompileShader(
		L"content/shaders/particles.hlsl", nullptr, "PS_Particle", "ps_5_0");
	m_particleSimCb = std::make_unique<GpuUploadBuffer<ParticleSimConstants>>(device, 1u, true);
	m_particleDrawCb = std::make_unique<GpuUploadBuffer<ParticleDrawConstants>>(device, 1u, true);

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 6u;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_particleUavHeap)));

	const UINT particleStride = static_cast<UINT>(sizeof(ParticleGpu));
	const UINT64 particleBytes = static_cast<UINT64>(particleStride) * static_cast<UINT64>(m_particleMaxCount);

	for (UINT i = 0; i < 2u; ++i)
	{
		CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
		CD3DX12_RESOURCE_DESC descParticles = CD3DX12_RESOURCE_DESC::Buffer(
			particleBytes,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		ThrowIfFailed(device->CreateCommittedResource(
			&heapDefault,
			D3D12_HEAP_FLAG_NONE,
			&descParticles,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			IID_PPV_ARGS(&m_particleBuffers[i])));

		CD3DX12_RESOURCE_DESC descCounter = CD3DX12_RESOURCE_DESC::Buffer(
			4u,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		ThrowIfFailed(device->CreateCommittedResource(
			&heapDefault,
			D3D12_HEAP_FLAG_NONE,
			&descCounter,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			IID_PPV_ARGS(&m_particleCounters[i])));
	}

	// Upload буферы-источники для быстрого сброса counters.
	{
		CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC desc4 = CD3DX12_RESOURCE_DESC::Buffer(4u);
		ThrowIfFailed(device->CreateCommittedResource(
			&heapUpload,
			D3D12_HEAP_FLAG_NONE,
			&desc4,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_particleCounterResetZero)));
		ThrowIfFailed(device->CreateCommittedResource(
			&heapUpload,
			D3D12_HEAP_FLAG_NONE,
			&desc4,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_particleCounterResetMax)));

		void* p0 = nullptr;
		ThrowIfFailed(m_particleCounterResetZero->Map(0, nullptr, &p0));
		*reinterpret_cast<uint32_t*>(p0) = 0u;
		m_particleCounterResetZero->Unmap(0, nullptr);

		void* p1 = nullptr;
		ThrowIfFailed(m_particleCounterResetMax->Map(0, nullptr, &p1));
		*reinterpret_cast<uint32_t*>(p1) = m_particleMaxCount;
		m_particleCounterResetMax->Unmap(0, nullptr);
	}

	m_particleUavDescriptorSize =
		device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE hCpu = m_particleUavHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT pair = 0; pair < 2u; ++pair)
	{
		const UINT consumeIdx = (pair == 0u) ? 0u : 1u;
		const UINT appendIdx = (pair == 0u) ? 1u : 0u;

		D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = DXGI_FORMAT_UNKNOWN;
		uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uav.Buffer.FirstElement = 0;
		uav.Buffer.NumElements = m_particleMaxCount;
		uav.Buffer.StructureByteStride = particleStride;
		uav.Buffer.CounterOffsetInBytes = 0;
		uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
		device->CreateUnorderedAccessView(
			m_particleBuffers[consumeIdx].Get(),
			m_particleCounters[consumeIdx].Get(),
			&uav,
			hCpu);
		hCpu.ptr += m_particleUavDescriptorSize;
		device->CreateUnorderedAccessView(
			m_particleBuffers[appendIdx].Get(),
			m_particleCounters[appendIdx].Get(),
			&uav,
			hCpu);
		hCpu.ptr += m_particleUavDescriptorSize;
	}

	for (UINT i = 0; i < 2u; ++i)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = DXGI_FORMAT_UNKNOWN;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srv.Buffer.FirstElement = 0u;
		srv.Buffer.NumElements = m_particleMaxCount;
		srv.Buffer.StructureByteStride = static_cast<UINT>(sizeof(ParticleGpu));
		srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		device->CreateShaderResourceView(m_particleBuffers[i].Get(), &srv, hCpu);
		hCpu.ptr += m_particleUavDescriptorSize;
	}

	BuildParticleComputePipeline(device);
	BuildParticleDrawPipeline(device);
}

void RenderingSystem::UpdateParticles(ID3D12GraphicsCommandList* cmd, float deltaTime)
{
	if (!cmd || !m_psoParticleCompute || !m_particleUavHeap || m_particleMaxCount == 0u)
		return;

	ParticleSimConstants sim{};
	sim.DeltaTime = deltaTime;
	sim.Gravity = -9.8f;
	sim.MaxParticles = m_particleMaxCount;
	sim.SpawnCount = 0u;
	sim.EmitterPos = m_particleEmitterPos;
	m_particleSimCb->CopyData(0, sim);

	ID3D12DescriptorHeap* heaps[] = {m_particleUavHeap.Get()};
	cmd->SetDescriptorHeaps(static_cast<UINT>(_countof(heaps)), heaps);

	const UINT consumeIdx = m_particlePingPong;
	const UINT appendIdx = 1u - m_particlePingPong;

	// Если буфер читался как SRV в графике, вернуть его в UAV для compute.
	for (UINT i = 0; i < 2u; ++i)
	{
		if (!m_particleBufferIsSrv[i])
			continue;
		auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(
			m_particleBuffers[i].Get(),
			D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->ResourceBarrier(1u, &toUav);
		m_particleBufferIsSrv[i] = false;
	}

	ResetCounter(cmd, m_particleCounters[consumeIdx].Get(), m_particleCounterResetMax.Get());
	ResetCounter(cmd, m_particleCounters[appendIdx].Get(), m_particleCounterResetZero.Get());

	cmd->SetComputeRootSignature(m_rsParticleCompute.Get());
	cmd->SetPipelineState(m_psoParticleCompute.Get());
	cmd->SetComputeRootConstantBufferView(0u, m_particleSimCb->Resource()->GetGPUVirtualAddress());

	CD3DX12_GPU_DESCRIPTOR_HANDLE uavBase(
		m_particleUavHeap->GetGPUDescriptorHandleForHeapStart(),
		static_cast<INT>(m_particlePingPong * 2u),
		m_particleUavDescriptorSize);
	cmd->SetComputeRootDescriptorTable(1u, uavBase);

	const UINT groups = (m_particleMaxCount + 63u) / 64u;
	cmd->Dispatch(groups, 1u, 1u);

	m_particlePingPong = 1u - m_particlePingPong;
}

void RenderingSystem::DrawParticles(
	ID3D12GraphicsCommandList* cmd,
	D3D12_CPU_DESCRIPTOR_HANDLE colorTargetRtv,
	D3D12_CPU_DESCRIPTOR_HANDLE sceneDepthDsv,
	bool useGBufferDepth,
	const D3D12_VIEWPORT& viewport,
	const D3D12_RECT& scissor,
	CXMMATRIX view,
	CXMMATRIX proj,
	FXMVECTOR cameraRight,
	FXMVECTOR cameraUp)
{
	ID3D12PipelineState* particlePso = m_psoParticleDraw.Get();
	if (useGBufferDepth)
	{
		if (!m_psoParticleDraw)
			return;
	}
	else
	{
		particlePso = m_psoParticleDrawNoDepth.Get();
		if (!particlePso)
			return;
	}

	if (!cmd || !m_particleDrawCb || m_particleMaxCount == 0u)
		return;

	ParticleDrawConstants cb{};
	XMStoreFloat4x4(&cb.ViewProj, XMMatrixTranspose(view * proj));
	XMStoreFloat3(&cb.CameraRight, cameraRight);
	XMStoreFloat3(&cb.CameraUp, cameraUp);
	m_particleDrawCb->CopyData(0, cb);

	if (useGBufferDepth)
		cmd->OMSetRenderTargets(1u, &colorTargetRtv, FALSE, &sceneDepthDsv);
	else
		cmd->OMSetRenderTargets(1u, &colorTargetRtv, FALSE, nullptr);

	cmd->RSSetViewports(1u, &viewport);
	cmd->RSSetScissorRects(1u, &scissor);

	ID3D12DescriptorHeap* heaps[] = {m_particleUavHeap.Get()};
	cmd->SetDescriptorHeaps(static_cast<UINT>(_countof(heaps)), heaps);
	cmd->SetGraphicsRootSignature(m_rsParticleDraw.Get());
	cmd->SetPipelineState(particlePso);
	cmd->SetGraphicsRootConstantBufferView(0u, m_particleDrawCb->Resource()->GetGPUVirtualAddress());

	const UINT drawIdx = m_particlePingPong;
	const UINT srvIndex = 4u + drawIdx;
	if (!m_particleBufferIsSrv[drawIdx])
	{
		auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
			m_particleBuffers[drawIdx].Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1u, &toSrv);
		m_particleBufferIsSrv[drawIdx] = true;
	}
	CD3DX12_GPU_DESCRIPTOR_HANDLE srv(
		m_particleUavHeap->GetGPUDescriptorHandleForHeapStart(),
		static_cast<INT>(srvIndex),
		m_particleUavDescriptorSize);
	cmd->SetGraphicsRootDescriptorTable(1u, srv);

	cmd->IASetVertexBuffers(0u, 0u, nullptr);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
	cmd->DrawInstanced(m_particleMaxCount, 1u, 0u, 0u);
}

void RenderingSystem::BuildShadowPipeline(ID3D12Device* device)
{
	m_shadowVsBc = Dx12Utils::CompileShader(
		L"content/shaders/shadow_depth.hlsl", nullptr, "VS_Shadow", "vs_5_0");
	ComPtr<ID3DBlob> shadowPsBc = Dx12Utils::CompileShader(
		L"content/shaders/shadow_depth.hlsl", nullptr, "PS_Shadow", "ps_5_0");

	CD3DX12_DESCRIPTOR_RANGE srvRange{};
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 0u);

	CD3DX12_ROOT_PARAMETER rp[2]{};
	rp[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
	rp[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_STATIC_SAMPLER_DESC samplerDesc(
		0,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	CD3DX12_ROOT_SIGNATURE_DESC rsd{};
	rsd.Init(
		static_cast<UINT>(_countof(rp)),
		rp,
		1,
		&samplerDesc,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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
		IID_PPV_ARGS(&m_rsShadow)));

	D3D12_INPUT_ELEMENT_DESC shadowLayout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.InputLayout = {shadowLayout, _countof(shadowLayout)};
	pso.pRootSignature = m_rsShadow.Get();
	pso.VS = {m_shadowVsBc->GetBufferPointer(), m_shadowVsBc->GetBufferSize()};
	pso.PS = {shadowPsBc->GetBufferPointer(), shadowPsBc->GetBufferSize()};
	CD3DX12_RASTERIZER_DESC rs(D3D12_DEFAULT);
	rs.CullMode = D3D12_CULL_MODE_BACK;
	rs.DepthBias = 50000;
	rs.DepthBiasClamp = 0.0f;
	rs.SlopeScaledDepthBias = 1.0f;
	pso.RasterizerState = rs;
	pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	CD3DX12_DEPTH_STENCIL_DESC ds(D3D12_DEFAULT);
	ds.DepthEnable = TRUE;
	ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	pso.DepthStencilState = ds;
	pso.SampleMask = UINT_MAX;
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.NumRenderTargets = 0u;
	pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pso.SampleDesc.Count = 1u;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoShadow)));

	rs.CullMode = D3D12_CULL_MODE_NONE;
	pso.RasterizerState = rs;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoShadowNoCull)));
}

void RenderingSystem::InitializeShadows(ID3D12Device* device)
{
	m_shadows.Initialize(device);
}

void RenderingSystem::ResizeShadows(ID3D12Device* device)
{
	m_shadows.Resize(device);
}

void RenderingSystem::UpdateShadowCascades(
	CXMMATRIX view,
	CXMMATRIX proj,
	const XMFLOAT3& lightDirWorld,
	const XMFLOAT3& eyeWorld,
	const XMFLOAT3& cameraForwardWorld,
	const Aabb& sceneBounds,
	float cameraNear,
	float cameraFar,
	float cameraFovYRad,
	float cameraAspect)
{
	const XMMATRIX invView = XMMatrixInverse(nullptr, view);
	const XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
	m_shadows.UpdateCascades(
		view,
		proj,
		invView,
		invProj,
		lightDirWorld,
		eyeWorld,
		cameraForwardWorld,
		sceneBounds,
		cameraNear,
		cameraFar,
		cameraFovYRad,
		cameraAspect);
	m_shadowLightingCb->CopyData(0, m_shadows.GetLightingConstants());
}

void RenderingSystem::BeginShadowPass(ID3D12GraphicsCommandList* cmd, UINT cascadeIndex)
{
	if (!cmd || cascadeIndex >= kShadowCascadeCount)
		return;

	if (m_shadows.IsShaderReadable())
	{
		auto toDepth = CD3DX12_RESOURCE_BARRIER::Transition(
			m_shadows.ShadowMapResource(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_DEPTH_WRITE);
		cmd->ResourceBarrier(1u, &toDepth);
		m_shadows.SetShaderReadable(false);
	}

	const D3D12_VIEWPORT vp = {
		0.0f,
		0.0f,
		static_cast<float>(kShadowMapSize),
		static_cast<float>(kShadowMapSize),
		0.0f,
		1.0f};
	const D3D12_RECT scissor = {0, 0, static_cast<LONG>(kShadowMapSize), static_cast<LONG>(kShadowMapSize)};
	cmd->RSSetViewports(1u, &vp);
	cmd->RSSetScissorRects(1u, &scissor);

	const D3D12_CPU_DESCRIPTOR_HANDLE cascadeDsv = m_shadows.CascadeDsv(cascadeIndex);
	cmd->OMSetRenderTargets(0u, nullptr, false, &cascadeDsv);
	cmd->ClearDepthStencilView(
		cascadeDsv,
		D3D12_CLEAR_FLAG_DEPTH,
		1.0f,
		0u,
		0,
		nullptr);

	cmd->SetPipelineState(m_psoShadow.Get());
	cmd->SetGraphicsRootSignature(m_rsShadow.Get());
}

void RenderingSystem::EndShadowPass(ID3D12GraphicsCommandList* cmd)
{
	if (!cmd)
		return;

	auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
		m_shadows.ShadowMapResource(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmd->ResourceBarrier(1u, &toSrv);
	m_shadows.SetShaderReadable(true);
}

void RenderingSystem::SetShadowPipeline(ID3D12GraphicsCommandList* cmd, bool alphaTestCutout)
{
	if (!cmd)
		return;
	cmd->SetPipelineState((alphaTestCutout ? m_psoShadowNoCull : m_psoShadow).Get());
	cmd->SetGraphicsRootSignature(m_rsShadow.Get());
}

void RenderingSystem::BuildLightingPipeline(ID3D12Device* device)
{
	CD3DX12_DESCRIPTOR_RANGE srvRange{};
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 9u, 0u);

	CD3DX12_ROOT_PARAMETER rp[3]{};
	rp[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
	rp[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL);
	rp[2].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_STATIC_SAMPLER_DESC samplers[3]{};
	samplers[0].Init(
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
	samplers[1].Init(
		1,
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		0.f,
		16,
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
		0.f,
		D3D12_FLOAT32_MAX,
		D3D12_SHADER_VISIBILITY_PIXEL,
		0);
	samplers[2].Init(
		2,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		0.f,
		16,
		D3D12_COMPARISON_FUNC_NEVER,
		D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
		0.f,
		D3D12_FLOAT32_MAX,
		D3D12_SHADER_VISIBILITY_PIXEL,
		0);

	CD3DX12_ROOT_SIGNATURE_DESC rsd{};
	rsd.NumParameters = 3;
	rsd.pParameters = rp;
	rsd.NumStaticSamplers = 3;
	rsd.pStaticSamplers = samplers;
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
	m_post.Resize(device, width, height);
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

	D3D12_CPU_DESCRIPTOR_HANDLE shadowSlot = h;
	shadowSlot.ptr += static_cast<SIZE_T>(heapOffsetFirst + GBuffer::kRtCount + 1u) * descriptorIncrementSize;
	device->CopyDescriptorsSimple(
		1u,
		shadowSlot,
		m_shadows.ShadowSrvCpu(),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	const UINT iblBase = heapOffsetFirst + GBuffer::kRtCount + 2u;
	if (m_iblIrradiance)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE slot = h;
		slot.ptr += static_cast<SIZE_T>(iblBase + 0u) * descriptorIncrementSize;
		Dx12Utils::CreateTextureSrv(device, m_iblIrradiance.Get(), slot, m_iblIrradianceIsCubemap);
	}
	if (m_iblPrefilteredEnv)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE slot = h;
		slot.ptr += static_cast<SIZE_T>(iblBase + 1u) * descriptorIncrementSize;
		Dx12Utils::CreateTextureSrv(device, m_iblPrefilteredEnv.Get(), slot, m_iblPrefilterIsCubemap);
	}
	if (m_iblIntegrationMap)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE slot = h;
		slot.ptr += static_cast<SIZE_T>(iblBase + 2u) * descriptorIncrementSize;
		Dx12Utils::CreateTextureSrv(device, m_iblIntegrationMap.Get(), slot, m_iblIntegrationIsCubemap);
	}

	const UINT postSrvOffset = heapOffsetFirst + GBuffer::kRtCount + 2u + kIblSrvCount;
	m_post.CreateSrvs(device, postSrvOffset, descriptorIncrementSize, shaderVisibleSrvHeap);
}

void RenderingSystem::RunPostProcess(
	ID3D12GraphicsCommandList* cmd,
	ID3D12Resource* backBuffer,
	D3D12_RESOURCE_STATES backBufferStateBefore,
	ID3D12DescriptorHeap* srvHeap,
	UINT deferredSrvHeapBase,
	UINT srvDescriptorIncrement,
	D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,
	const D3D12_VIEWPORT& viewport,
	const D3D12_RECT& scissor)
{
	if (!UsesSceneColorTarget())
		return;

	const UINT postSrvBase = deferredSrvHeapBase + GBuffer::kRtCount + 2u + kIblSrvCount;
	m_post.Run(
		cmd,
		backBuffer,
		backBufferStateBefore,
		srvHeap,
		postSrvBase,
		srvDescriptorIncrement,
		backBufferRtv,
		viewport,
		scissor,
		m_postVignetteEnabled,
		m_postChromaticEnabled,
		m_postGrayscaleEnabled);
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
	c.MaxEnvMipLevel = m_iblMaxEnvMipLevel;
	c.HasIblEnv = m_iblPrefilteredEnv ? 1.0f : 0.0f;
	c.UseBeckmann = m_useBeckmannBrdf ? 1.0f : 0.0f;
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
