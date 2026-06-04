#pragma once

#include "ShadowStructures.h"

#include "../math/BoundingBox.h"

#include <d3d12.h>
#include <wrl/client.h>

class CascadedShadowMaps
{
public:
	void Initialize(ID3D12Device* device);
	void Resize(ID3D12Device* device);

	void UpdateCascades(
		DirectX::CXMMATRIX view,
		DirectX::CXMMATRIX proj,
		DirectX::CXMMATRIX invView,
		DirectX::CXMMATRIX invProj,
		const DirectX::XMFLOAT3& lightDirWorld,
		const DirectX::XMFLOAT3& eyeWorld,
		const DirectX::XMFLOAT3& cameraForwardWorld,
		const Aabb& sceneBounds,
		float cameraNear,
		float cameraFar,
		float cameraFovYRad,
		float cameraAspect);

	const ShadowLightingConstants& GetLightingConstants() const { return m_shadowLighting; }

	D3D12_CPU_DESCRIPTOR_HANDLE CascadeDsv(uint32_t cascade) const;
	uint32_t DsvDescriptorIncrement() const { return m_dsvDescriptorIncrement; }
	D3D12_CPU_DESCRIPTOR_HANDLE ShadowSrvCpu() const { return m_shadowSrvCpu; }

	ID3D12Resource* ShadowMapResource() const { return m_shadowMap.Get(); }
	bool IsShaderReadable() const { return m_isShaderReadable; }
	void SetShaderReadable(bool v) { m_isShaderReadable = v; }

private:
	void ComputeCascadeSplits(float nearZ, float farZ);
	void BuildCascadeMatrices(
		DirectX::CXMMATRIX view,
		DirectX::CXMMATRIX proj,
		DirectX::CXMMATRIX invView,
		DirectX::CXMMATRIX invProj,
		const DirectX::XMFLOAT3& lightDirWorld,
		const DirectX::XMFLOAT3& eyeWorld,
		const DirectX::XMFLOAT3& cameraForwardWorld,
		const Aabb& sceneBounds,
		float cameraFovYRad,
		float cameraAspect);

	Microsoft::WRL::ComPtr<ID3D12Resource> m_shadowMap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
	D3D12_CPU_DESCRIPTOR_HANDLE m_shadowSrvCpu{};

	float m_splitDistances[kShadowCascadeCount + 1] = {};
	ShadowLightingConstants m_shadowLighting{};
	bool m_isShaderReadable = false;
	uint32_t m_dsvDescriptorIncrement = 0u;
};
