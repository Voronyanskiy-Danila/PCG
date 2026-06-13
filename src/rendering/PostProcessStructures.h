#pragma once

#include <cstddef>
#include <cstdint>

// Lab 7 — constant buffer b0 (post_process.hlsl PostCB), 48-byte HLSL layout
struct PostProcessConstants
{
	float VignetteStrength = 2.0f;
	float VignettePower = 1.15f;
	float ChromaticStrength = 0.006f;
	float ChromaticRadial = 1.5f;
	float _pad0 = 0.0f;
	float _padToFloat3Align[3] = {};
	float _hlslFloat3Slot[4] = {};
};

static_assert(sizeof(PostProcessConstants) == 48u, "PostProcessConstants must be 48 bytes");
static_assert(
	offsetof(PostProcessConstants, _hlslFloat3Slot) == 32u,
	"PostProcessConstants must match HLSL float3 gPad1 at offset 32");
