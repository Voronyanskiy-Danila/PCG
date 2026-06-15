#include "LightRainCircleRenderer.h"
#include "LightRainSystem.h"

#include "../rendering/d3d12/D3d12_RenderHelpers.h"
#include "../rendering/d3d12/d3dx12.h"

#include <cstring>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

void LightRainCircleRenderer::Initialize(ID3D12Device* device, DXGI_FORMAT rtFormat, UINT maxCircles)
{
	if (!device || maxCircles == 0u)
		return;

	m_maxCircles = maxCircles;
	BuildPipeline(device, rtFormat);

	m_drawCb = std::make_unique<GpuUploadBuffer<ParticleDrawConstants>>(device, 1u, true);

	const UINT byteSize = m_maxCircles * static_cast<UINT>(sizeof(RainCircleGpu));
	CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(byteSize));
	ThrowIfFailed(device->CreateCommittedResource(
		&heap,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_circleBuffer)));
	ThrowIfFailed(m_circleBuffer->Map(0, nullptr, &m_circleMapped));
	std::memset(m_circleMapped, 0, byteSize);

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.NumDescriptors = 1u;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap)));
	m_srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_SHADER_RESOURCE_VIEW_DESC sbv{};
	sbv.Format = DXGI_FORMAT_UNKNOWN;
	sbv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sbv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	sbv.Buffer.FirstElement = 0u;
	sbv.Buffer.NumElements = m_maxCircles;
	sbv.Buffer.StructureByteStride = static_cast<UINT>(sizeof(RainCircleGpu));
	sbv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	device->CreateShaderResourceView(
		m_circleBuffer.Get(),
		&sbv,
		m_srvHeap->GetCPUDescriptorHandleForHeapStart());
}

void LightRainCircleRenderer::BuildPipeline(ID3D12Device* device, DXGI_FORMAT rtFormat)
{
	m_vsBc = Dx12Utils::CompileShader(
		L"content/shaders/light_rain_circles.hlsl", nullptr, "VS_RainCircle", "vs_5_0");
	m_gsBc = Dx12Utils::CompileShader(
		L"content/shaders/light_rain_circles.hlsl", nullptr, "GS_RainCircle", "gs_5_0");
	m_psBc = Dx12Utils::CompileShader(
		L"content/shaders/light_rain_circles.hlsl", nullptr, "PS_RainCircle", "ps_5_0");

	CD3DX12_DESCRIPTOR_RANGE srvRange{};
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 0u);

	CD3DX12_ROOT_PARAMETER rp[2]{};
	rp[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
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
		IID_PPV_ARGS(&m_rootSignature)));

	auto makeBlendDesc = []() {
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
	pso.pRootSignature = m_rootSignature.Get();
	pso.VS = {m_vsBc->GetBufferPointer(), m_vsBc->GetBufferSize()};
	pso.GS = {m_gsBc->GetBufferPointer(), m_gsBc->GetBufferSize()};
	pso.PS = {m_psBc->GetBufferPointer(), m_psBc->GetBufferSize()};
	CD3DX12_RASTERIZER_DESC rs(D3D12_DEFAULT);
	rs.CullMode = D3D12_CULL_MODE_NONE;
	pso.RasterizerState = rs;
	pso.BlendState = makeBlendDesc();
	CD3DX12_DEPTH_STENCIL_DESC ds(D3D12_DEFAULT);
	ds.DepthEnable = TRUE;
	ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	pso.DepthStencilState = ds;
	pso.SampleMask = UINT_MAX;
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	pso.NumRenderTargets = 1u;
	pso.RTVFormats[0] = rtFormat;
	pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pso.SampleDesc.Count = 1u;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoWithDepth)));

	CD3DX12_DEPTH_STENCIL_DESC dsNoDepth(D3D12_DEFAULT);
	dsNoDepth.DepthEnable = FALSE;
	pso.DepthStencilState = dsNoDepth;
	pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoNoDepth)));
}

void LightRainCircleRenderer::Draw(
	ID3D12GraphicsCommandList* cmd,
	const LightRainSystem& rain,
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
	if (!cmd || !m_circleMapped || !m_drawCb || m_maxCircles == 0u || !rain.IsEnabled())
		return;

	const UINT dropCount = rain.FillCircleDrawData(
		reinterpret_cast<RainCircleGpu*>(m_circleMapped),
		m_maxCircles);
	if (dropCount == 0u)
		return;

	ID3D12PipelineState* pso = useGBufferDepth ? m_psoWithDepth.Get() : m_psoNoDepth.Get();
	if (!pso)
		return;

	ParticleDrawConstants cb{};
	XMStoreFloat4x4(&cb.ViewProj, XMMatrixTranspose(view * proj));
	XMStoreFloat3(&cb.CameraRight, cameraRight);
	XMStoreFloat3(&cb.CameraUp, cameraUp);
	m_drawCb->CopyData(0, cb);

	if (useGBufferDepth)
		cmd->OMSetRenderTargets(1u, &colorTargetRtv, FALSE, &sceneDepthDsv);
	else
		cmd->OMSetRenderTargets(1u, &colorTargetRtv, FALSE, nullptr);

	cmd->RSSetViewports(1u, &viewport);
	cmd->RSSetScissorRects(1u, &scissor);

	ID3D12DescriptorHeap* heaps[] = {m_srvHeap.Get()};
	cmd->SetDescriptorHeaps(static_cast<UINT>(_countof(heaps)), heaps);
	cmd->SetGraphicsRootSignature(m_rootSignature.Get());
	cmd->SetPipelineState(pso);
	cmd->SetGraphicsRootConstantBufferView(0u, m_drawCb->Resource()->GetGPUVirtualAddress());
	cmd->SetGraphicsRootDescriptorTable(
		1u,
		m_srvHeap->GetGPUDescriptorHandleForHeapStart());

	cmd->IASetVertexBuffers(0u, 0u, nullptr);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
	cmd->DrawInstanced(dropCount, 1u, 0u, 0u);
}
