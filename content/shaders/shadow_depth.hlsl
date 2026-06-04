cbuffer ShadowDrawCB : register(b0)
{
	float4x4 gWorldLightViewProj;
};

struct VSInput
{
	float3 PosL    : POSITION;
	float3 NormalL : NORMAL;
	float2 Tex     : TEXCOORD0;
};

float4 VS_Shadow(VSInput vin) : SV_POSITION
{
	return mul(float4(vin.PosL, 1.0f), gWorldLightViewProj);
}

void PS_Shadow()
{
}
