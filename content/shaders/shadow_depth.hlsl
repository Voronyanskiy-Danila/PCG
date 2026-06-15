cbuffer ShadowDrawCB : register(b0)
{
	float4x4 gWorldLightViewProj;
	// Lab 1: те же параметры squash/stretch, что ObjectCB (вазы Sponza)
	float gVertexAnimEnable;
	float gVertexAnimPivotX;
	float gVertexAnimPivotY;
	float gVertexAnimPivotZ;
	float gVertexAnimPhase;
	float gVertexAnimTime;
	float gVertexAnimAmp;
	float gVertexAnimSpeed;
	// Lab 6 доп: alpha test по diffuse (тени в форме текстуры)
	float gAlphaTestEnable;
	float gAlphaTestCutoff;
};

Texture2D gDiffuseMap : register(t0);
SamplerState gSamLinearWrap : register(s0);

struct VSInput
{
	float3 PosL    : POSITION;
	float3 NormalL : NORMAL;
	float2 Tex     : TEXCOORD0;
};

struct VSOutput
{
	float4 PosH : SV_POSITION;
	float2 Tex  : TEXCOORD0;
};

float3 ApplyVaseVertexAnimShadow(float3 posL)
{
	if (gVertexAnimEnable < 0.5f)
		return posL;

	const float wave = sin(gVertexAnimTime * gVertexAnimSpeed + gVertexAnimPhase);
	const float sy = 1.0 + gVertexAnimAmp * wave;
	const float sxz = rsqrt(max(sy, 0.2));

	float3 animated = posL;
	animated.y = gVertexAnimPivotY + (animated.y - gVertexAnimPivotY) * sy;
	animated.x = gVertexAnimPivotX + (animated.x - gVertexAnimPivotX) * sxz;
	animated.z = gVertexAnimPivotZ + (animated.z - gVertexAnimPivotZ) * sxz;
	return animated;
}

VSOutput VS_Shadow(VSInput vin)
{
	VSOutput o;
	const float3 posL = ApplyVaseVertexAnimShadow(vin.PosL);
	o.PosH = mul(float4(posL, 1.0f), gWorldLightViewProj);
	o.Tex = vin.Tex;
	return o;
}

void PS_Shadow(VSOutput pin)
{
	if (gAlphaTestEnable > 0.5f)
	{
		const float4 tex = gDiffuseMap.Sample(gSamLinearWrap, pin.Tex);
		const float mask = max(tex.a, tex.r);
		clip(mask - gAlphaTestCutoff);
	}
}
