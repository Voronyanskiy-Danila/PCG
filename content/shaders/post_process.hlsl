// post_process.hlsl — Lab 7: fullscreen vignette + chromatic aberration

Texture2D gSceneColor : register(t0);
SamplerState gLinearClamp : register(s0);

cbuffer PostCB : register(b0)
{
	float gVignetteStrength;
	float gVignettePower;
	float gChromaticStrength;
	float gChromaticRadial;
	float gPad0;
	float3 gPad1;
};

struct VSOut
{
	float4 PosH : SV_POSITION;
	float2 TexC : TEXCOORD0;
};

VSOut VS_Post(uint vid : SV_VertexID)
{
	VSOut o;
	float2 full = float2((vid << 1u) & 2u, vid & 2u);
	o.PosH = float4(full.x * 2.f - 1.f, -full.y * 2.f + 1.f, 0.f, 1.f);
	o.TexC = float2(full.x * 0.5f, full.y * 0.5f);
	return o;
}

float2 ScreenUv(float4 posSs, uint w, uint h)
{
	return posSs.xy / float2(max(w, 1u), max(h, 1u));
}

// Post 1: затемнение к краям экрана (виньетка)
float4 PS_Vignette(VSOut pin, float4 posSs : SV_Position) : SV_TARGET
{
	uint w, h, levels;
	gSceneColor.GetDimensions(0, w, h, levels);
	float2 uv = ScreenUv(posSs, w, h);

	float4 c = gSceneColor.Sample(gLinearClamp, uv);
	float2 d = uv - 0.5;
	float vig = 1.0 - dot(d, d) * gVignetteStrength;
	vig = pow(saturate(vig), gVignettePower);
	return float4(c.rgb * vig, c.a);
}

// Post 2: хроматическая аберрация — R/B смещаются от центра (имитация линзы)
float4 PS_ChromaticAberration(VSOut pin, float4 posSs : SV_Position) : SV_TARGET
{
	uint w, h, levels;
	gSceneColor.GetDimensions(0, w, h, levels);
	float2 uv = ScreenUv(posSs, w, h);

	float2 toCenter = uv - 0.5;
	float radial = pow(length(toCenter) * 2.0, gChromaticRadial);
	float2 dir = normalize(toCenter + 1e-5) * (gChromaticStrength * radial);

	float r = gSceneColor.Sample(gLinearClamp, uv - dir).r;
	float g = gSceneColor.Sample(gLinearClamp, uv).g;
	float b = gSceneColor.Sample(gLinearClamp, uv + dir).b;
	float a = gSceneColor.Sample(gLinearClamp, uv).a;
	return float4(r, g, b, a);
}
