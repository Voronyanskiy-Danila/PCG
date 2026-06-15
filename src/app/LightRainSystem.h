#pragma once

// Lab 2 доп: point lights падают дождём на пол Sponza и остаются светиться.

#include <Windows.h>

#include "../rendering/GpuLightStructures.h"

#include <DirectXMath.h>
#include <vector>

struct RainCircleGpu
{
	DirectX::XMFLOAT3 Position{};
	float Size = 1.0f;
	DirectX::XMFLOAT4 Color{1.0f, 1.0f, 1.0f, 1.0f};
	UINT Landed = 0u;
	UINT Pad[3]{};
};
static_assert(sizeof(RainCircleGpu) == 48u, "RainCircleGpu must match HLSL");

class LightRainSystem
{
public:
	void Configure(
		const DirectX::XMFLOAT3& areaCenter,
		float floorY,
		float halfExtentX,
		float halfExtentZ,
		float spawnHeightMin);

	void Update(float deltaTime);

	void SetEnabled(bool enabled) { m_enabled = enabled; }
	bool IsEnabled() const { return m_enabled; }

	void AppendPointLights(std::vector<GpuLight>& out) const;

	UINT FillCircleDrawData(RainCircleGpu* out, UINT maxCount) const;

	UINT ActiveDropCount() const { return static_cast<UINT>(m_drops.size()); }
	UINT LandedDropCount() const;

private:
	struct RainDrop
	{
		DirectX::XMFLOAT3 Position{};
		float VelocityY = 0.0f;
		bool Landed = false;
		DirectX::XMFLOAT3 Color{0.75f, 0.88f, 1.0f};
		float Intensity = 0.0f;
		float Range = 0.0f;
		float CircleSize = 7.0f;

		GpuLight ToGpuLight() const;
	};

	void SpawnDrop();
	bool RemoveOldestFalling();
	UINT CountFalling() const;

	float Rand01();

	bool m_enabled = true;
	bool m_configured = false;
	DirectX::XMFLOAT3 m_center{};
	float m_floorY = 0.0f;
	float m_halfExtentX = 10.0f;
	float m_halfExtentZ = 10.0f;
	float m_spawnHeightMin = 20.0f;
	float m_spawnHeightJitter = 10.0f;

	float m_spawnAccumulator = 0.0f;
	float m_spawnInterval = 0.08f;
	float m_fallSpeed = 32.0f;
	UINT m_maxFallingDrops = 15u;
	UINT m_maxTotalDrops = 512u;

	UINT m_rngState = 0xC0FFEEu;
	std::vector<RainDrop> m_drops;
};
