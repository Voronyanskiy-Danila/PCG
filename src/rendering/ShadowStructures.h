#pragma once

#include <DirectXMath.h>
#include <cstdint>

constexpr uint32_t kShadowCascadeCount = 3u;
constexpr uint32_t kShadowMapSize = 2048u;
constexpr uint32_t kMaxShadowDrawCalls = 4096u;
constexpr float kShadowSplitLambda = 0.75f;
constexpr float kShadowDepthBias = 0.00005f;

struct ShadowDrawConstants
{
	DirectX::XMFLOAT4X4 WorldLightViewProj = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f};
	// Lab 1 — vertex squash/stretch (must match shadow_depth.hlsl ShadowDrawCB)
	float VertexAnimEnable = 0.0f;
	float VertexAnimPivotX = 0.0f;
	float VertexAnimPivotY = 0.0f;
	float VertexAnimPivotZ = 0.0f;
	float VertexAnimPhase = 0.0f;
	float VertexAnimTime = 0.0f;
	float VertexAnimAmp = 0.18f;
	float VertexAnimSpeed = 2.0f;
	// Lab 6 доп — alpha test в shadow / G-buffer (забор и т.п.)
	float AlphaTestEnable = 0.0f;
	float AlphaTestCutoff = 0.5f;
};

struct ShadowLightingConstants
{
	DirectX::XMFLOAT4X4 LightViewProj[kShadowCascadeCount] = {};
	DirectX::XMFLOAT4X4 ViewMatrix = {};
	DirectX::XMFLOAT4 CascadeSplits = {};
	float CameraNear = 0.5f;
	float ShadowBias = kShadowDepthBias;
	DirectX::XMFLOAT2 InvShadowMapSize = {1.0f / kShadowMapSize, 1.0f / kShadowMapSize};
};

static_assert(
	sizeof(ShadowLightingConstants) == 288u,
	"ShadowLightingConstants must match deferred_lighting.hlsl ShadowLightingCB");
static_assert(
	((sizeof(ShadowLightingConstants) + 255u) & ~255u) == 512u,
	"ShadowLightingConstants CB must round to 512 bytes for D3D12");
