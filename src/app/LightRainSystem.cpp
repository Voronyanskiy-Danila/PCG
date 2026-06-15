#include "LightRainSystem.h"

#include <algorithm>

using namespace DirectX;

namespace
{

constexpr float kRainIntensityFall = 52000.0f;
constexpr float kRainIntensityLanded = 42000.0f;
constexpr float kRainRangeFall = 36.0f;
constexpr float kRainRangeLanded = 32.0f;

} // namespace

void LightRainSystem::Configure(
	const XMFLOAT3& areaCenter,
	float floorY,
	float halfExtentX,
	float halfExtentZ,
	float spawnHeightMin)
{
	m_center = areaCenter;
	m_floorY = floorY;
	m_halfExtentX = (std::max)(halfExtentX, 2.0f);
	m_halfExtentZ = (std::max)(halfExtentZ, 2.0f);
	m_spawnHeightMin = spawnHeightMin;
	m_configured = true;
	m_drops.clear();
	m_spawnAccumulator = 0.0f;
}

float LightRainSystem::Rand01()
{
	m_rngState = m_rngState * 1664525u + 1013904223u;
	return static_cast<float>(m_rngState & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

GpuLight LightRainSystem::RainDrop::ToGpuLight() const
{
	GpuLight light{};
	light.Type = kLightTypePoint;
	light.Position = Position;
	light.Direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
	light.Range = Range;
	light.Color = Color;
	light.Intensity = Intensity;
	light.SpotInnerCos = 0.0f;
	light.SpotOuterCos = 0.0f;
	light.Padding = XMFLOAT2(0.0f, 0.0f);
	return light;
}

void LightRainSystem::SpawnDrop()
{
	RainDrop drop{};
	const float rx = (Rand01() * 2.0f - 1.0f) * m_halfExtentX;
	const float rz = (Rand01() * 2.0f - 1.0f) * m_halfExtentZ;
	drop.Position = XMFLOAT3(m_center.x + rx, m_spawnHeightMin + Rand01() * m_spawnHeightJitter, m_center.z + rz);
	drop.VelocityY = -m_fallSpeed * (0.85f + Rand01() * 0.3f);
	drop.Landed = false;

	const float tint = Rand01();
	drop.Color = XMFLOAT3(
		0.55f + tint * 0.35f,
		0.78f + tint * 0.18f,
		0.95f + tint * 0.05f);
	drop.Intensity = kRainIntensityFall;
	drop.Range = kRainRangeFall;
	drop.CircleSize = 6.0f + Rand01() * 4.0f;
	m_drops.push_back(drop);
}

bool LightRainSystem::RemoveOldestFalling()
{
	for (auto it = m_drops.begin(); it != m_drops.end(); ++it)
	{
		if (!it->Landed)
		{
			m_drops.erase(it);
			return true;
		}
	}
	return false;
}

UINT LightRainSystem::CountFalling() const
{
	UINT count = 0u;
	for (const RainDrop& drop : m_drops)
	{
		if (!drop.Landed)
			++count;
	}
	return count;
}

void LightRainSystem::Update(float deltaTime)
{
	if (!m_enabled || !m_configured || deltaTime <= 0.0f)
		return;

	for (RainDrop& drop : m_drops)
	{
		if (drop.Landed)
			continue;

		drop.Position.y += drop.VelocityY * deltaTime;
		if (drop.Position.y <= m_floorY)
		{
			drop.Position.y = m_floorY;
			drop.Landed = true;
			drop.VelocityY = 0.0f;
			drop.Intensity = kRainIntensityLanded;
			drop.Range = kRainRangeLanded;
			drop.CircleSize = 12.0f + Rand01() * 6.0f;
		}
	}

	m_spawnAccumulator += deltaTime;
	while (m_spawnAccumulator >= m_spawnInterval)
	{
		m_spawnAccumulator -= m_spawnInterval;

		if (CountFalling() >= m_maxFallingDrops)
			continue;

		if (m_drops.size() >= m_maxTotalDrops)
		{
			if (!RemoveOldestFalling())
				continue;
		}

		SpawnDrop();
	}
}

void LightRainSystem::AppendPointLights(std::vector<GpuLight>& out) const
{
	if (!m_enabled)
		return;

	for (const RainDrop& drop : m_drops)
	{
		if (!drop.Landed)
			continue;
		if (out.size() >= kDeferredMaxLights)
			break;
		out.push_back(drop.ToGpuLight());
	}

	for (const RainDrop& drop : m_drops)
	{
		if (drop.Landed)
			continue;
		if (out.size() >= kDeferredMaxLights)
			break;
		out.push_back(drop.ToGpuLight());
	}
}

UINT LightRainSystem::LandedDropCount() const
{
	UINT count = 0u;
	for (const RainDrop& drop : m_drops)
	{
		if (drop.Landed)
			++count;
	}
	return count;
}

UINT LightRainSystem::FillCircleDrawData(RainCircleGpu* out, UINT maxCount) const
{
	if (!m_enabled || !out || maxCount == 0u)
		return 0u;

	const UINT count = static_cast<UINT>((std::min)(m_drops.size(), static_cast<size_t>(maxCount)));
	for (UINT i = 0u; i < count; ++i)
	{
		const RainDrop& drop = m_drops[i];
		out[i].Position = drop.Position;
		out[i].Size = drop.CircleSize;
		const float alpha = drop.Landed ? 0.92f : 0.78f;
		out[i].Color = XMFLOAT4(drop.Color.x, drop.Color.y, drop.Color.z, alpha);
		out[i].Landed = drop.Landed ? 1u : 0u;
		out[i].Pad[0] = out[i].Pad[1] = out[i].Pad[2] = 0u;
	}
	return count;
}
