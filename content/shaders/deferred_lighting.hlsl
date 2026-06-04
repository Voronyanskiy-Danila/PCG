Texture2D    gBufAlbedo   : register(t0);
Texture2D    gBufNormal   : register(t1);
Texture2D    gBufPosition : register(t2);
Texture2D    gBufMatExtra : register(t3);

static const uint kLightTypeDirectional = 0;
static const uint kLightTypePoint = 1;
static const uint kLightTypeSpot = 2;

struct GpuLight
{
	float3 Position;
	uint   Type;
	float3 Direction;
	float  Range;
	float3 Color;
	float  Intensity;
	float  SpotInnerCos;
	float  SpotOuterCos;
	float2 Padding;
};

StructuredBuffer<GpuLight> gLights : register(t4);
Texture2DArray<float> gShadowMap : register(t5);

cbuffer LightingCB : register(b0)
{
	float4 gEyeWorld;
	float4 gDirLightDirection;
	float4 gDirColorIntensity;
	uint   gNumLights;
	uint   _p0;
	uint   _p1;
	uint   _p2;
};

cbuffer ShadowLightingCB : register(b1)
{
	float4x4 gLightViewProj[3];
	float4x4 gViewMatrix;
	float4   gCascadeSplits;
	float    gCameraNear;
	float    gShadowBias;
	float2   gShadowMapInvSize;
};

SamplerState gPointClamp : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

struct VSQuadOut
{
	float4 PosH : SV_POSITION;
	float2 TexC : TEXCOORD0;
};

VSQuadOut VS_Light(uint vid : SV_VertexID)
{
	VSQuadOut o;
	float2 full = float2((vid << 1u) & 2u, vid & 2u);
	o.PosH = float4(full.x * 2.f - 1.f, -full.y * 2.f + 1.f, 0.f, 1.f);
	o.TexC = float2(full.x * 0.5f, full.y * 0.5f);
	return o;
}

float3 ReinhardToneMap(float3 x)
{
	return x / (float3(1.f, 1.f, 1.f) + x);
}

float GetViewDepth(float3 worldPos)
{
	float4 v = mul(float4(worldPos, 1.0f), gViewMatrix);
	return max(v.z, 0.0f);
}

uint SelectCascade(float viewDepth)
{
	if (viewDepth < gCascadeSplits.x)
		return 0u;
	if (viewDepth < gCascadeSplits.y)
		return 1u;
	if (viewDepth < gCascadeSplits.z)
		return 2u;
	return 2u;
}

float SampleShadowPcf(uint cascade, float3 worldPos)
{
	float4 posL = mul(float4(worldPos, 1.0f), gLightViewProj[cascade]);
	float3 proj = posL.xyz / max(abs(posL.w), 1e-5f);
	float2 uv = proj.xy * 0.5f + 0.5f;
	uv.y = 1.0f - uv.y;

	// Orthographic light projection: clip Z is already in [0, 1] for D3D.
	float depth = saturate(proj.z);

	if (any(uv < 0.0f) || any(uv > 1.0f))
		return 1.0f;

	float shadow = 0.0f;
	[unroll]
	for (int y = -1; y <= 1; ++y)
	{
		[unroll]
		for (int x = -1; x <= 1; ++x)
		{
			float2 offset = float2(x, y) * gShadowMapInvSize;
			shadow += gShadowMap.SampleCmpLevelZero(
				gShadowSampler,
				float3(uv + offset, (float)cascade),
				depth - gShadowBias);
		}
	}
	return shadow / 9.0f;
}

float CalcDirectionalShadow(float3 worldPos)
{
	if (dot(worldPos, worldPos) < 1e-4f)
		return 1.0f;

	const float viewDepth = GetViewDepth(worldPos);
	uint cascade = SelectCascade(viewDepth);
	float s = SampleShadowPcf(cascade, worldPos);

	if (cascade > 0u)
	{
		float blendEnd;
		float blendRange;
		if (cascade == 1u)
		{
			blendEnd = gCascadeSplits.x;
			blendRange = max(blendEnd - gCameraNear, 1e-3f);
		}
		else
		{
			blendEnd = gCascadeSplits.y;
			blendRange = max(blendEnd - gCascadeSplits.x, 1e-3f);
		}
		const float t = saturate((viewDepth - (blendEnd - blendRange * 0.15f)) / (blendRange * 0.15f));
		const float sPrev = SampleShadowPcf(cascade - 1u, worldPos);
		s = lerp(sPrev, s, t);
	}

	return s;
}

float4 PS_Light(VSQuadOut pin, float4 posSs : SV_Position) : SV_Target
{
	uint w, h, levels;
	gBufAlbedo.GetDimensions(0, w, h, levels);
	float2 dims = float2(max(w, 1u), max(h, 1u));
	float2 uv = posSs.xy / dims;

	float4 albedoS = gBufAlbedo.Sample(gPointClamp, uv);
	float3 albedo = albedoS.rgb;

	float4 nS = gBufNormal.Sample(gPointClamp, uv);
	float nLen2 = dot(nS.xyz, nS.xyz);
	if (nLen2 < 1e-6f)
		return float4(0.04f, 0.05f, 0.08f, 1.f);

	float3 N = nS.xyz * rsqrt(nLen2);

	float4 pS = gBufPosition.Sample(gPointClamp, uv);
	float3 pw = pS.xyz;
	if (dot(pw, pw) < 1e-4f)
		return float4(0.04f, 0.05f, 0.08f, 1.f);

	float4 mx = gBufMatExtra.Sample(gPointClamp, uv);
	float3 matKs = mx.rgb;
	float specNorm = saturate(mx.a);

	float3 eye = gEyeWorld.xyz;
	float3 vw = eye - pw;
	float vLen2 = dot(vw, vw);
	float3 V = vLen2 > 1e-10f ? vw * rsqrt(vLen2) : float3(0.f, 0.f, -1.f);

	float3 diffuseAcc = float3(0, 0, 0);
	float3 specAcc = float3(0, 0, 0);

	const float shininess = lerp(1.f, 256.f, specNorm * specNorm);

	float3 dd = gDirLightDirection.xyz;
	float dDirs = dot(dd, dd);
	if (dDirs > 1e-8f && dot(pw, pw) > 1e-4f)
	{
		float3 L = normalize(-dd);
		float3 sunCol = gDirColorIntensity.xyz * gDirColorIntensity.w;
		float shadow = CalcDirectionalShadow(pw);

		float NdotL = saturate(dot(N, L));
		float3 diffuse = NdotL * albedo;

		float3 hv = L + V;
		float hh = dot(hv, hv);
		float3 H = hh > 1e-12f ? hv * rsqrt(hh) : N;
		float nh = saturate(dot(N, H));
		float specAmt = pow(max(nh, 0.f), shininess) * NdotL;

		diffuseAcc += diffuse * sunCol * shadow;
		specAcc += specAmt * matKs * sunCol * shadow;
	}

	const uint MAX_L = 48u;
	uint nLights = min(gNumLights, MAX_L);

	for (uint i = 0u; i < nLights; ++i)
	{
		GpuLight Ld = gLights[i];
		if (Ld.Type == kLightTypeDirectional)
			continue;

		float3 col = Ld.Color * Ld.Intensity;

		if (Ld.Type == kLightTypePoint)
		{
			float3 toL = Ld.Position - pw;
			float dist = length(toL);
			if (dist < 1e-5f || dist > Ld.Range)
				continue;

			float3 L = toL / dist;
			float NdotL = saturate(dot(N, L));
			float3 diffuse = NdotL * albedo;

			float3 hvp = L + V;
			float hhp = dot(hvp, hvp);
			float3 Hpt = hhp > 1e-12f ? hvp * rsqrt(hhp) : N;
			float nhP = saturate(dot(N, Hpt));
			float specAmt = pow(max(nhP, 0.f), shininess) * NdotL;

			float rp = saturate(1.f - dist / max(Ld.Range, 1e-4f));
			float att = rp * rp;

			diffuseAcc += diffuse * col * att;
			specAcc += specAmt * matKs * col * att;
		}
		else if (Ld.Type == kLightTypeSpot)
		{
			float3 toL = Ld.Position - pw;
			float dist = length(toL);
			if (dist < 1e-5f || dist > Ld.Range)
				continue;

			float3 L = toL / dist;
			float3 dirL = Ld.Direction;
			float dLen2 = dot(dirL, dirL);
			if (dLen2 < 1e-8f)
				continue;
			dirL = normalize(dirL);
			float cosAngle = dot(-L, dirL);
			float outer = max(Ld.SpotOuterCos, 1e-4f);

			float spot = saturate((cosAngle - outer) / max(Ld.SpotInnerCos - outer, 1e-4f));
			if (spot <= 0.f)
				continue;

			float NdotL = saturate(dot(N, L));
			float3 diffuse = NdotL * albedo;

			float3 hvs = L + V;
			float hhs = dot(hvs, hvs);
			float3 Hst = hhs > 1e-12f ? hvs * rsqrt(hhs) : N;
			float nhS = saturate(dot(N, Hst));
			float specAmt = pow(max(nhS, 0.f), shininess) * NdotL;

			float rq = saturate(1.f - dist / max(Ld.Range, 1e-4f));
			float att = rq * rq * spot;

			diffuseAcc += diffuse * col * att;
			specAcc += specAmt * matKs * col * att;
		}
	}

	float ambient = 0.12f;
	float3 hdrLinear = diffuseAcc + specAcc + ambient * albedo;

	return float4(ReinhardToneMap(hdrLinear), 1.f);
}
