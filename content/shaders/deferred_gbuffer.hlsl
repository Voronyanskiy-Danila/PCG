// G-buffer fill: те же параметры модели и UV, что в phong.hlsl.
cbuffer ObjectCB : register(b0)
{
	float4x4 gWorld;
	float4x4 gWorldInvTranspose;
	float4x4 gWorldViewProj;

	float3   gEyePosW;
	float    gSpecPower;

	float3   gLightDirW;
	float    gAmbientK;

	float3   gLightColor;
	float    _pad0;

	float3   gMatKa;
	float    gHasDiffuseTexture;

	float3   gMatKd;
	float    gMatNs;

	float3   gMatKs;
	float    _pad1;

	float2   gUvScale;
	float2   gUvScroll;
	// .x = время (с); .y = 1 — движение UV («живые» текстуры), 0 — только тайлинг по gUvScale.
	float4   gUvAnimParams;
};

Texture2D    gDiffuseMap : register(t0);
SamplerState gSamLinearWrap : register(s0);

struct VSInput
{
	float3 PosL    : POSITION;
	float3 NormalL : NORMAL;
	float2 Tex     : TEXCOORD0;
};

struct PSInput
{
	float4 PosH    : SV_POSITION;
	float3 PosW    : TEXCOORD0;
	float3 NormalW : TEXCOORD1;
	float2 TexAnim : TEXCOORD2;
};

PSInput VS(VSInput vin)
{
	PSInput vout;

	float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
	vout.PosW = posW.xyz;

	float3 nWorld = mul(float4(vin.NormalL, 0.0f), gWorldInvTranspose).xyz;
	vout.NormalW = normalize(nWorld);

	vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);

	float t = gUvAnimParams.x;
	const float move = gUvAnimParams.y;
	float2 uv = vin.Tex * gUvScale + move * (t * gUvScroll);
	uv.x += move * sin(t * 0.35 + vin.Tex.y * 5.0) * 0.012;
	uv.y += move * sin(t * 0.3 + vin.Tex.x * 4.5) * 0.01;
	vout.TexAnim = uv;

	return vout;
}

struct GBufferPack
{
	float4 AlbedoA     : SV_Target0;
	float4 NormalW     : SV_Target1;
	float4 PosWorld    : SV_Target2;
	float4 KsRoughPad  : SV_Target3;
};

GBufferPack PS(PSInput pin)
{
	GBufferPack o;

	float3 texRgb = gDiffuseMap.Sample(gSamLinearWrap, pin.TexAnim).rgb;
	float3 N = normalize(pin.NormalW);
	float3 alb = gMatKd * lerp(float3(1, 1, 1), texRgb, gHasDiffuseTexture);

	o.AlbedoA = float4(alb, 1.f);
	o.NormalW = float4(N, 0.f);
	o.PosWorld = float4(pin.PosW, 1.f);
	float rough = saturate(gMatNs / 128.f);
	o.KsRoughPad = float4(gMatKs, rough);

	return o;
}
