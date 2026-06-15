#include "DistantBillboardRenderer.h"

#include "../rendering/GBuffer.h"
#include "../rendering/d3d12/D3d12_RenderHelpers.h"
#include "../rendering/d3d12/d3dx12.h"

#include <cstring>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

void DistantBillboardRenderer::Initialize(
	ID3D12Device* device,
	ID3D12DescriptorHeap* shaderVisibleSrvHeap,
	UINT instanceSrvHeapIndex,
	UINT srvDescriptorIncrement)
{
	if (!device || !shaderVisibleSrvHeap)
		return;

	m_instanceSrvHeapIndex = instanceSrvHeapIndex;
	m_srvDescriptorIncrement = srvDescriptorIncrement;

	BuildPipeline(device);
	m_materialCb = std::make_unique<GpuUploadBuffer<BillboardMaterialConstants>>(device, 1u, true);

	const UINT byteSize = m_maxInstances * static_cast<UINT>(sizeof(BillboardGpu));
	CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(byteSize));
	ThrowIfFailed(device->CreateCommittedResource(
		&heap,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_instanceBuffer)));
	ThrowIfFailed(m_instanceBuffer->Map(0, nullptr, &m_instanceMapped));
	std::memset(m_instanceMapped, 0, byteSize);

	D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = shaderVisibleSrvHeap->GetCPUDescriptorHandleForHeapStart();
	srvCpu.ptr += static_cast<SIZE_T>(instanceSrvHeapIndex) * static_cast<SIZE_T>(srvDescriptorIncrement);

	D3D12_SHADER_RESOURCE_VIEW_DESC sbv{};
	sbv.Format = DXGI_FORMAT_UNKNOWN;
	sbv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sbv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	sbv.Buffer.FirstElement = 0u;
	sbv.Buffer.NumElements = m_maxInstances;
	sbv.Buffer.StructureByteStride = static_cast<UINT>(sizeof(BillboardGpu));
	sbv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	device->CreateShaderResourceView(m_instanceBuffer.Get(), &sbv, srvCpu);
}

void DistantBillboardRenderer::BuildPipeline(ID3D12Device* device)
{
	m_vsBc = Dx12Utils::CompileShader(
		L"content/shaders/billboard_gbuffer.hlsl", nullptr, "VS_Billboard", "vs_5_0");
	m_gsBc = Dx12Utils::CompileShader(
		L"content/shaders/billboard_gbuffer.hlsl", nullptr, "GS_Billboard", "gs_5_0");
	m_psBc = Dx12Utils::CompileShader(
		L"content/shaders/billboard_gbuffer.hlsl", nullptr, "PS_Billboard", "ps_5_0");

	CD3DX12_DESCRIPTOR_RANGE srvRanges[3]{};
	srvRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 0u);
	srvRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 1u);
	srvRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 2u);

	CD3DX12_ROOT_PARAMETER rp[4]{};
	rp[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
	rp[1].InitAsDescriptorTable(1, &srvRanges[0], D3D12_SHADER_VISIBILITY_ALL);
	rp[2].InitAsDescriptorTable(1, &srvRanges[1], D3D12_SHADER_VISIBILITY_ALL);
	rp[3].InitAsDescriptorTable(1, &srvRanges[2], D3D12_SHADER_VISIBILITY_ALL);

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
		IID_PPV_ARGS(&m_rootSignature)));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.InputLayout = {nullptr, 0u};
	pso.pRootSignature = m_rootSignature.Get();
	pso.VS = {m_vsBc->GetBufferPointer(), m_vsBc->GetBufferSize()};
	pso.GS = {m_gsBc->GetBufferPointer(), m_gsBc->GetBufferSize()};
	pso.PS = {m_psBc->GetBufferPointer(), m_psBc->GetBufferSize()};
	CD3DX12_RASTERIZER_DESC rs(D3D12_DEFAULT);
	rs.CullMode = D3D12_CULL_MODE_NONE;
	pso.RasterizerState = rs;
	pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pso.SampleMask = UINT_MAX;
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	pso.NumRenderTargets = GBuffer::kRtCount;
	for (UINT i = 0u; i < GBuffer::kRtCount; ++i)
		pso.RTVFormats[i] = GBuffer::RtFormat(i);
	pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pso.SampleDesc.Count = 1u;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso)));
}

void DistantBillboardRenderer::DrawGBuffer(
	ID3D12GraphicsCommandList* cmd,
	const std::vector<BillboardGpu>& instances,
	const BillboardMaterialConstants& material,
	ID3D12DescriptorHeap* srvHeap,
	UINT instanceSrvHeapIndex,
	UINT materialSrvBase,
	UINT srvDescriptorIncrement)
{
	if (!cmd || !m_instanceMapped || instances.empty() || !m_pso || !srvHeap)
		return;

	const UINT count = static_cast<UINT>((std::min)(instances.size(), static_cast<size_t>(m_maxInstances)));
	std::memcpy(m_instanceMapped, instances.data(), sizeof(BillboardGpu) * count);

	m_materialCb->CopyData(0, material);

	ID3D12DescriptorHeap* heaps[] = {srvHeap};
	cmd->SetDescriptorHeaps(1u, heaps);

	cmd->SetGraphicsRootSignature(m_rootSignature.Get());
	cmd->SetPipelineState(m_pso.Get());
	cmd->SetGraphicsRootConstantBufferView(0u, m_materialCb->Resource()->GetGPUVirtualAddress());

	CD3DX12_GPU_DESCRIPTOR_HANDLE instH(srvHeap->GetGPUDescriptorHandleForHeapStart());
	instH.Offset(static_cast<INT>(instanceSrvHeapIndex), static_cast<INT>(srvDescriptorIncrement));
	cmd->SetGraphicsRootDescriptorTable(1u, instH);

	CD3DX12_GPU_DESCRIPTOR_HANDLE diffuseH(srvHeap->GetGPUDescriptorHandleForHeapStart());
	diffuseH.Offset(static_cast<INT>(materialSrvBase), static_cast<INT>(srvDescriptorIncrement));
	cmd->SetGraphicsRootDescriptorTable(2u, diffuseH);

	CD3DX12_GPU_DESCRIPTOR_HANDLE rmH(diffuseH);
	rmH.Offset(3, static_cast<INT>(srvDescriptorIncrement));
	cmd->SetGraphicsRootDescriptorTable(3u, rmH);

	cmd->IASetVertexBuffers(0u, 0u, nullptr);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
	cmd->DrawInstanced(count, 1u, 0u, 0u);
}
