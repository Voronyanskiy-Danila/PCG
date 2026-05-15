#pragma once

#include <DirectXMath.h>
#include <cstdint>

constexpr UINT kGpuLightStride = 64u;
constexpr UINT kDeferredMaxLights = 48u;

constexpr UINT kLightTypeDirectional = 0u;
constexpr UINT kLightTypePoint = 1u;
constexpr UINT kLightTypeSpot = 2u;

#pragma pack(push, 16)
struct GpuLight
{
	DirectX::XMFLOAT3 Position{};
	UINT Type{};
	DirectX::XMFLOAT3 Direction{};
	float Range = 1e20f;
	DirectX::XMFLOAT3 Color{ 1.f, 1.f, 1.f };
	float Intensity = 1.0f;
	float SpotInnerCos = 0.97f;
	float SpotOuterCos = 0.92f;
	DirectX::XMFLOAT2 Padding{};
};
#pragma pack(pop)
static_assert(sizeof(GpuLight) == kGpuLightStride, "GpuLight size mismatch");

// Только поля, реально читаемые в deferred_lighting.hlsl — layout должен совпадать с HLSL cbuffer.
struct DeferredLightingConstants
{
	DirectX::XMFLOAT4 EyeWorld{};
	UINT NumLights = 0;
	UINT _Pad0 = 0;
	UINT _Pad1 = 0;
	UINT _Pad2 = 0;
};
static_assert(sizeof(DeferredLightingConstants) == 32u, "DeferredLightingConstants size");
