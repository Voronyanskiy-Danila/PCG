#include "PostProcess.h"

#include "../rendering/d3d12/D3d12_RenderHelpers.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace
{

void CreateColorSrv(
	ID3D12Device* device,
	ID3D12Resource* tex,
	DXGI_FORMAT srvFormat,
	D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC d{};
	d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	d.Format = srvFormat;
	d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	d.Texture2D.MipLevels = 1u;
	d.Texture2D.MostDetailedMip = 0u;
	d.Texture2D.ResourceMinLODClamp = 0.f;
	device->CreateShaderResourceView(tex, &d, dst);
}

void DrawFullscreen(
	ID3D12GraphicsCommandList* cmd,
	ID3D12PipelineState* pso,
	ID3D12RootSignature* rs,
	D3D12_GPU_VIRTUAL_ADDRESS cbGpu,
	CD3DX12_GPU_DESCRIPTOR_HANDLE sceneSrvGpu)
{
	cmd->SetPipelineState(pso);
	cmd->SetGraphicsRootSignature(rs);
	cmd->SetGraphicsRootConstantBufferView(0u, cbGpu);
	cmd->SetGraphicsRootDescriptorTable(1u, sceneSrvGpu);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->IASetVertexBuffers(0u, 0u, nullptr);
	cmd->DrawInstanced(3u, 1u, 0u, 0u);
}

} // namespace

void PostProcess::Initialize(ID3D12Device* device, DXGI_FORMAT rtFormat)
{
	m_rtFormat = rtFormat;
	m_constantsCb = std::make_unique<GpuUploadBuffer<PostProcessConstants>>(device, 1u, true);

	m_vsBc = Dx12Utils::CompileShader(
		L"content/shaders/post_process.hlsl", nullptr, "VS_Post", "vs_5_0");
	m_psVignetteBc = Dx12Utils::CompileShader(
		L"content/shaders/post_process.hlsl", nullptr, "PS_Vignette", "ps_5_0");
	m_psChromaticBc = Dx12Utils::CompileShader(
		L"content/shaders/post_process.hlsl", nullptr, "PS_ChromaticAberration", "ps_5_0");

	BuildPipelines(device);
}

void PostProcess::Resize(ID3D12Device* device, UINT width, UINT height)
{
	m_width = width;
	m_height = height;
	if (!device || width == 0u || height == 0u)
		return;

	m_sceneColor.Reset();
	m_tempColor.Reset();
	m_rtvHeap.Reset();
	m_sceneState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	m_tempState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	CreateTargets(device);
}

void PostProcess::TransitionResource(
	ID3D12GraphicsCommandList* cmd,
	ID3D12Resource* res,
	D3D12_RESOURCE_STATES& trackedState,
	D3D12_RESOURCE_STATES newState)
{
	if (!cmd || !res || trackedState == newState)
		return;
	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(res, trackedState, newState);
	cmd->ResourceBarrier(1u, &barrier);
	trackedState = newState;
}

void PostProcess::CreateTargets(ID3D12Device* device)
{
	m_rtvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
	rtvDesc.NumDescriptors = 2u;
	rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ThrowIfFailed(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap)));

	CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
	const float clearColor[4] = {0.f, 0.f, 0.f, 0.f};
	CD3DX12_CLEAR_VALUE clearValue(m_rtFormat, clearColor);
	CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		m_rtFormat,
		m_width,
		m_height,
		1u,
		1u,
		1u,
		0u,
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

	ThrowIfFailed(device->CreateCommittedResource(
		&heapDefault,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&clearValue,
		IID_PPV_ARGS(&m_sceneColor)));
	ThrowIfFailed(device->CreateCommittedResource(
		&heapDefault,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&clearValue,
		IID_PPV_ARGS(&m_tempColor)));

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	device->CreateRenderTargetView(m_sceneColor.Get(), nullptr, rtv);
	rtv.ptr += m_rtvIncrement;
	device->CreateRenderTargetView(m_tempColor.Get(), nullptr, rtv);

	D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
	srvDesc.NumDescriptors = 2u;
	srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ComPtr<ID3D12DescriptorHeap> srvHeap;
	ThrowIfFailed(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap)));
	m_sceneSrvCpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
	m_tempSrvCpu = m_sceneSrvCpu;
	m_tempSrvCpu.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	CreateColorSrv(device, m_sceneColor.Get(), m_rtFormat, m_sceneSrvCpu);
	CreateColorSrv(device, m_tempColor.Get(), m_rtFormat, m_tempSrvCpu);
}

D3D12_CPU_DESCRIPTOR_HANDLE PostProcess::SceneRtv() const
{
	return m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
}

void PostProcess::CreateSrvs(
	ID3D12Device* device,
	UINT heapOffsetFirst,
	UINT descriptorIncrementSize,
	ID3D12DescriptorHeap* shaderVisibleSrvHeap)
{
	if (!device || !shaderVisibleSrvHeap || !m_sceneColor)
		return;

	D3D12_CPU_DESCRIPTOR_HANDLE dst = shaderVisibleSrvHeap->GetCPUDescriptorHandleForHeapStart();
	dst.ptr += static_cast<SIZE_T>(heapOffsetFirst) * descriptorIncrementSize;
	device->CopyDescriptorsSimple(1u, dst, m_sceneSrvCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	dst.ptr += descriptorIncrementSize;
	device->CopyDescriptorsSimple(1u, dst, m_tempSrvCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void PostProcess::BuildPipelines(ID3D12Device* device)
{
	CD3DX12_DESCRIPTOR_RANGE srvRange{};
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 0u);

	CD3DX12_ROOT_PARAMETER rp[2]{};
	rp[0].InitAsConstantBufferView(0u, 0u, D3D12_SHADER_VISIBILITY_PIXEL);
	rp[1].InitAsDescriptorTable(1u, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_STATIC_SAMPLER_DESC sampler{};
	sampler.Init(
		0u,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	CD3DX12_ROOT_SIGNATURE_DESC rsd{};
	rsd.Init(_countof(rp), rp, 1u, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE);

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
		0u,
		serialized->GetBufferPointer(),
		serialized->GetBufferSize(),
		IID_PPV_ARGS(&m_rootSignature)));

	auto makePso = [&](ID3DBlob* ps) {
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
		pso.pRootSignature = m_rootSignature.Get();
		pso.VS = {m_vsBc->GetBufferPointer(), m_vsBc->GetBufferSize()};
		pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
		CD3DX12_RASTERIZER_DESC rs(D3D12_DEFAULT);
		rs.CullMode = D3D12_CULL_MODE_NONE;
		pso.RasterizerState = rs;
		pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		CD3DX12_DEPTH_STENCIL_DESC ds(D3D12_DEFAULT);
		ds.DepthEnable = FALSE;
		pso.DepthStencilState = ds;
		pso.SampleMask = UINT_MAX;
		pso.SampleDesc.Count = 1u;
		pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pso.NumRenderTargets = 1u;
		pso.RTVFormats[0] = m_rtFormat;
		ComPtr<ID3D12PipelineState> out;
		ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&out)));
		return out;
	};

	m_psoVignette = makePso(m_psVignetteBc.Get());
	m_psoChromatic = makePso(m_psChromaticBc.Get());
}

void PostProcess::Run(
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
	bool chromaticEnabled)
{
	if (!cmd || !backBuffer || !m_sceneColor || !m_psoVignette || !m_psoChromatic || !m_constantsCb)
		return;
	if (!vignetteEnabled && !chromaticEnabled)
		return;

	PostProcessConstants cb{};
	m_constantsCb->CopyData(0, cb);
	const D3D12_GPU_VIRTUAL_ADDRESS cbGpu = m_constantsCb->Resource()->GetGPUVirtualAddress();

	ID3D12DescriptorHeap* heaps[] = {srvHeap};
	cmd->SetDescriptorHeaps(1u, heaps);
	cmd->RSSetViewports(1u, &viewport);
	cmd->RSSetScissorRects(1u, &scissor);

	const UINT sceneSrvIndex = srvHeapBaseOffset;
	const UINT tempSrvIndex = srvHeapBaseOffset + 1u;
	CD3DX12_GPU_DESCRIPTOR_HANDLE sceneSrvGpu(
		srvHeap->GetGPUDescriptorHandleForHeapStart(),
		static_cast<INT>(sceneSrvIndex),
		srvDescriptorIncrement);
	CD3DX12_GPU_DESCRIPTOR_HANDLE tempSrvGpu(
		srvHeap->GetGPUDescriptorHandleForHeapStart(),
		static_cast<INT>(tempSrvIndex),
		srvDescriptorIncrement);

	D3D12_CPU_DESCRIPTOR_HANDLE tempRtvHandle = SceneRtv();
	tempRtvHandle.ptr += m_rtvIncrement;

	TransitionResource(
		cmd,
		m_sceneColor.Get(),
		m_sceneState,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	D3D12_RESOURCE_STATES backBufferState = backBufferStateBefore;
	TransitionResource(
		cmd,
		backBuffer,
		backBufferState,
		D3D12_RESOURCE_STATE_RENDER_TARGET);

	if (vignetteEnabled && chromaticEnabled)
	{
		TransitionResource(
			cmd,
			m_tempColor.Get(),
			m_tempState,
			D3D12_RESOURCE_STATE_RENDER_TARGET);

		cmd->OMSetRenderTargets(1u, &tempRtvHandle, FALSE, nullptr);
		DrawFullscreen(cmd, m_psoVignette.Get(), m_rootSignature.Get(), cbGpu, sceneSrvGpu);

		TransitionResource(
			cmd,
			m_tempColor.Get(),
			m_tempState,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		cmd->OMSetRenderTargets(1u, &backBufferRtv, FALSE, nullptr);
		DrawFullscreen(cmd, m_psoChromatic.Get(), m_rootSignature.Get(), cbGpu, tempSrvGpu);

		TransitionResource(
			cmd,
			m_tempColor.Get(),
			m_tempState,
			D3D12_RESOURCE_STATE_RENDER_TARGET);
	}
	else if (vignetteEnabled)
	{
		cmd->OMSetRenderTargets(1u, &backBufferRtv, FALSE, nullptr);
		DrawFullscreen(cmd, m_psoVignette.Get(), m_rootSignature.Get(), cbGpu, sceneSrvGpu);
	}
	else
	{
		cmd->OMSetRenderTargets(1u, &backBufferRtv, FALSE, nullptr);
		DrawFullscreen(cmd, m_psoChromatic.Get(), m_rootSignature.Get(), cbGpu, sceneSrvGpu);
	}

	TransitionResource(
		cmd,
		m_sceneColor.Get(),
		m_sceneState,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
}
