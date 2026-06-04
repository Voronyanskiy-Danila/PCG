#include "CascadedShadowMaps.h"

#include "../rendering/d3d12/D3d12_RenderHelpers.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{

XMFLOAT3 ToFloat3(FXMVECTOR v)
{
	XMFLOAT3 out{};
	XMStoreFloat3(&out, v);
	return out;
}

float LengthFloat3(const XMFLOAT3& v)
{
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

void BuildCascadeFrustumCornersWorld(
	CXMMATRIX view,
	float fovYRad,
	float aspect,
	float splitNear,
	float splitFar,
	std::array<XMFLOAT3, 8>& outCorners)
{
	const XMMATRIX sliceProj = XMMatrixPerspectiveFovLH(fovYRad, aspect, splitNear, splitFar);
	const XMMATRIX invSliceVp = XMMatrixInverse(nullptr, view * sliceProj);

	const XMFLOAT3 ndcCorners[8] = {
		{-1.0f, -1.0f, 0.0f},
		{1.0f, -1.0f, 0.0f},
		{1.0f, 1.0f, 0.0f},
		{-1.0f, 1.0f, 0.0f},
		{-1.0f, -1.0f, 1.0f},
		{1.0f, -1.0f, 1.0f},
		{1.0f, 1.0f, 1.0f},
		{-1.0f, 1.0f, 1.0f}};

	for (UINT i = 0u; i < 8u; ++i)
	{
		XMVECTOR p = XMVectorSet(ndcCorners[i].x, ndcCorners[i].y, ndcCorners[i].z, 1.0f);
		p = XMVector4Transform(p, invSliceVp);
		p = XMVectorScale(p, 1.0f / XMVectorGetW(p));
		outCorners[i] = ToFloat3(p);
	}
}

} // namespace

void CascadedShadowMaps::Initialize(ID3D12Device* device)
{
	Resize(device);
}

void CascadedShadowMaps::Resize(ID3D12Device* device)
{
	m_shadowMap.Reset();
	m_dsvHeap.Reset();
	m_srvHeap.Reset();
	m_isShaderReadable = false;

	CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R32_TYPELESS,
		kShadowMapSize,
		kShadowMapSize,
		static_cast<UINT16>(kShadowCascadeCount),
		1u,
		1u,
		0u,
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = DXGI_FORMAT_D32_FLOAT;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0u;

	ThrowIfFailed(device->CreateCommittedResource(
		&heapDefault,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clearValue,
		IID_PPV_ARGS(&m_shadowMap)));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
	dsvHeapDesc.NumDescriptors = kShadowCascadeCount;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
	srvHeapDesc.NumDescriptors = 1u;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ThrowIfFailed(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)));

	m_dsvDescriptorIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	D3D12_CPU_DESCRIPTOR_HANDLE dsvCpu = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	for (uint32_t i = 0u; i < kShadowCascadeCount; ++i)
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
		dsv.Format = DXGI_FORMAT_D32_FLOAT;
		dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
		dsv.Texture2DArray.MipSlice = 0u;
		dsv.Texture2DArray.FirstArraySlice = i;
		dsv.Texture2DArray.ArraySize = 1u;
		device->CreateDepthStencilView(m_shadowMap.Get(), &dsv, dsvCpu);
		dsvCpu.ptr += m_dsvDescriptorIncrement;
	}

	m_shadowSrvCpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = DXGI_FORMAT_R32_FLOAT;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srv.Texture2DArray.MostDetailedMip = 0u;
	srv.Texture2DArray.MipLevels = 1u;
	srv.Texture2DArray.FirstArraySlice = 0u;
	srv.Texture2DArray.ArraySize = kShadowCascadeCount;
	device->CreateShaderResourceView(m_shadowMap.Get(), &srv, m_shadowSrvCpu);

	m_shadowLighting.InvShadowMapSize = {
		1.0f / static_cast<float>(kShadowMapSize),
		1.0f / static_cast<float>(kShadowMapSize)};
	m_shadowLighting.ShadowBias = kShadowDepthBias;
}

void CascadedShadowMaps::ComputeCascadeSplits(float nearZ, float farZ)
{
	m_splitDistances[0] = nearZ;
	m_splitDistances[kShadowCascadeCount] = farZ;

	const float ratio = farZ / nearZ;
	for (UINT i = 1u; i < kShadowCascadeCount; ++i)
	{
		const float p = static_cast<float>(i) / static_cast<float>(kShadowCascadeCount);
		const float logSplit = nearZ * std::pow(ratio, p);
		const float uniSplit = nearZ + (farZ - nearZ) * p;
		m_splitDistances[i] = kShadowSplitLambda * logSplit + (1.0f - kShadowSplitLambda) * uniSplit;
	}

	m_shadowLighting.CascadeSplits = XMFLOAT4(
		m_splitDistances[1],
		m_splitDistances[2],
		m_splitDistances[3],
		0.0f);
}

void CascadedShadowMaps::BuildCascadeMatrices(
	CXMMATRIX view,
	CXMMATRIX proj,
	CXMMATRIX invView,
	CXMMATRIX invProj,
	const XMFLOAT3& lightDirWorld,
	const XMFLOAT3& eyeWorld,
	const XMFLOAT3& cameraForwardWorld,
	const Aabb& sceneBounds,
	float cameraFovYRad,
	float cameraAspect)
{
	(void)proj;
	(void)invView;
	(void)invProj;

	XMStoreFloat4x4(&m_shadowLighting.ViewMatrix, XMMatrixTranspose(view));

	XMVECTOR lightDir = XMLoadFloat3(&lightDirWorld);
	const float lightLen2 = XMVectorGetX(XMVector3LengthSq(lightDir));
	if (lightLen2 < 1e-8f)
		lightDir = XMVectorSet(0.35f, -0.85f, 0.38f, 0.0f);
	else
		lightDir = XMVector3Normalize(lightDir);

	XMVECTOR camForward = XMLoadFloat3(&cameraForwardWorld);
	const float fwdLen2 = XMVectorGetX(XMVector3LengthSq(camForward));
	if (fwdLen2 < 1e-8f)
		camForward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	else
		camForward = XMVector3Normalize(camForward);

	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	if (fabsf(XMVectorGetX(XMVector3Cross(lightDir, up))) < 0.15f)
		up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

	const XMVECTOR eye = XMLoadFloat3(&eyeWorld);
	constexpr float kLightViewBackDist = 5000.0f;

	for (uint32_t cascade = 0u; cascade < kShadowCascadeCount; ++cascade)
	{
		const float splitNear = m_splitDistances[cascade];
		const float splitFar = m_splitDistances[cascade + 1];

		std::array<XMFLOAT3, 8> frustumCorners{};
		BuildCascadeFrustumCornersWorld(
			view,
			cameraFovYRad,
			cameraAspect,
			splitNear,
			splitFar,
			frustumCorners);

		const float midSliceDist = 0.5f * (splitNear + splitFar);
		const XMVECTOR cascadeFocus = XMVectorAdd(eye, XMVectorScale(camForward, midSliceDist));
		const XMVECTOR lightEye = XMVectorSubtract(cascadeFocus, XMVectorScale(lightDir, kLightViewBackDist));

		const XMMATRIX lightView = XMMatrixLookAtLH(lightEye, cascadeFocus, up);

		float minX = FLT_MAX;
		float maxX = -FLT_MAX;
		float minY = FLT_MAX;
		float maxY = -FLT_MAX;
		float minZ = FLT_MAX;
		float maxZ = -FLT_MAX;

		for (const XMFLOAT3& c : frustumCorners)
		{
			const XMVECTOR v = XMVector3TransformCoord(XMLoadFloat3(&c), lightView);
			minX = (std::min)(minX, XMVectorGetX(v));
			maxX = (std::max)(maxX, XMVectorGetX(v));
			minY = (std::min)(minY, XMVectorGetY(v));
			maxY = (std::max)(maxY, XMVectorGetY(v));
			minZ = (std::min)(minZ, XMVectorGetZ(v));
			maxZ = (std::max)(maxZ, XMVectorGetZ(v));
		}

		if (sceneBounds.IsValid())
		{
			const XMFLOAT3 boundsPts[8] = {
				{sceneBounds.Min.x, sceneBounds.Min.y, sceneBounds.Min.z},
				{sceneBounds.Max.x, sceneBounds.Min.y, sceneBounds.Min.z},
				{sceneBounds.Min.x, sceneBounds.Max.y, sceneBounds.Min.z},
				{sceneBounds.Max.x, sceneBounds.Max.y, sceneBounds.Min.z},
				{sceneBounds.Min.x, sceneBounds.Min.y, sceneBounds.Max.z},
				{sceneBounds.Max.x, sceneBounds.Min.y, sceneBounds.Max.z},
				{sceneBounds.Min.x, sceneBounds.Max.y, sceneBounds.Max.z},
				{sceneBounds.Max.x, sceneBounds.Max.y, sceneBounds.Max.z}};
			for (const XMFLOAT3& p : boundsPts)
			{
				const XMVECTOR v = XMVector3TransformCoord(XMLoadFloat3(&p), lightView);
				minX = (std::min)(minX, XMVectorGetX(v));
				maxX = (std::max)(maxX, XMVectorGetX(v));
				minY = (std::min)(minY, XMVectorGetY(v));
				maxY = (std::max)(maxY, XMVectorGetY(v));
				minZ = (std::min)(minZ, XMVectorGetZ(v));
				maxZ = (std::max)(maxZ, XMVectorGetZ(v));
			}
		}

		const float sceneSpan = sceneBounds.IsValid()
			? LengthFloat3({
				sceneBounds.Max.x - sceneBounds.Min.x,
				sceneBounds.Max.y - sceneBounds.Min.y,
				sceneBounds.Max.z - sceneBounds.Min.z})
			: 500.0f;
		const float zPad = (std::max)(sceneSpan * 0.15f, 200.0f);
		minZ -= zPad;
		maxZ += zPad;

		const float extentX = (std::max)(maxX - minX, 1.0f);
		const float extentY = (std::max)(maxY - minY, 1.0f);
		const float orthoSize = (std::max)(extentX, extentY);
		const float texelWorld = orthoSize / static_cast<float>(kShadowMapSize);

		minX = std::floor(minX / texelWorld) * texelWorld;
		minY = std::floor(minY / texelWorld) * texelWorld;
		maxX = std::ceil(maxX / texelWorld) * texelWorld;
		maxY = std::ceil(maxY / texelWorld) * texelWorld;

		const float midX = 0.5f * (minX + maxX);
		const float midY = 0.5f * (minY + maxY);
		const float halfSize = 0.5f * (std::max)(maxX - minX, maxY - minY);
		minX = midX - halfSize;
		maxX = midX + halfSize;
		minY = midY - halfSize;
		maxY = midY + halfSize;

		const XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(
			minX,
			maxX,
			minY,
			maxY,
			minZ,
			maxZ);
		const XMMATRIX lightViewProj = lightView * lightProj;
		XMStoreFloat4x4(&m_shadowLighting.LightViewProj[cascade], XMMatrixTranspose(lightViewProj));
	}
}

void CascadedShadowMaps::UpdateCascades(
	CXMMATRIX view,
	CXMMATRIX proj,
	CXMMATRIX invView,
	CXMMATRIX invProj,
	const XMFLOAT3& lightDirWorld,
	const XMFLOAT3& eyeWorld,
	const XMFLOAT3& cameraForwardWorld,
	const Aabb& sceneBounds,
	float cameraNear,
	float cameraFar,
	float cameraFovYRad,
	float cameraAspect)
{
	ComputeCascadeSplits(cameraNear, cameraFar);
	m_shadowLighting.CameraNear = cameraNear;
	BuildCascadeMatrices(
		view,
		proj,
		invView,
		invProj,
		lightDirWorld,
		eyeWorld,
		cameraForwardWorld,
		sceneBounds,
		cameraFovYRad,
		cameraAspect);
}

D3D12_CPU_DESCRIPTOR_HANDLE CascadedShadowMaps::CascadeDsv(uint32_t cascade) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE h = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(cascade) * static_cast<SIZE_T>(m_dsvDescriptorIncrement);
	return h;
}
