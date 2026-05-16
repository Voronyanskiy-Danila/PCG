#pragma once

#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>

constexpr UINT kGpuLightStride = 64u;
constexpr UINT kDeferredMaxLights = 48u;

constexpr UINT kLightTypeDirectional = 0u;
constexpr UINT kLightTypePoint = 1u;
constexpr UINT kLightTypeSpot = 2u;

// То же упакование, что у struct GpuLight в deferred_lighting.hlsl (StructuredBuffer stride 64).
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
static_assert(sizeof(GpuLight) == kGpuLightStride, "GpuLight size mismatch");
static_assert(offsetof(GpuLight, Position) == 0u);
static_assert(offsetof(GpuLight, Type) == 12u);
static_assert(offsetof(GpuLight, Direction) == 16u);
static_assert(offsetof(GpuLight, Range) == 28u);
static_assert(offsetof(GpuLight, Color) == 32u);
static_assert(offsetof(GpuLight, Intensity) == 44u);
static_assert(offsetof(GpuLight, SpotInnerCos) == 48u);
static_assert(offsetof(GpuLight, SpotOuterCos) == 52u);
static_assert(offsetof(GpuLight, Padding) == 56u);

// cbufferLightingCB register(b0): все float4 сначала — совпадение упаковки с HLSL.
// Направленный свет (как в forward-пайплайне по LightDirW). StructuredBuffer — point/spot.
struct DeferredLightingConstants
{
	DirectX::XMFLOAT4 EyeWorld{};
	DirectX::XMFLOAT4 DirDirection{};           // xyz = нормализованный LightDirW, w = 0
	DirectX::XMFLOAT4 DirColorIntensity{};      // rgb = цвет, w = множитель (интенсивность)
	UINT NumLights = 0;
	UINT _Pad0 = 0;
	UINT _Pad1 = 0;
	UINT _Pad2 = 0;
};
static_assert(sizeof(DeferredLightingConstants) == 64u, "DeferredLightingConstants size");
