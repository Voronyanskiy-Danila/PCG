// Forward Phong: диффузная карта + Blinn–Phong (Sponza).
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
	float2 uv = vin.Tex * gUvScale + t * gUvScroll;
	uv.x += sin(t * 0.35 + vin.Tex.y * 5.0) * 0.012;
	uv.y += sin(t * 0.3 + vin.Tex.x * 4.5) * 0.01;
	vout.TexAnim = uv;

	return vout;
}

float4 PS(PSInput pin) : SV_Target
{
	float3 N = normalize(pin.NormalW);
	float3 texRgb = gDiffuseMap.Sample(gSamLinearWrap, pin.TexAnim).rgb;
	float3 albedo = gMatKd * lerp(float3(1, 1, 1), texRgb, gHasDiffuseTexture);

	float3 L = normalize(-gLightDirW);
	float NdotL = max(dot(N, L), 0.f);
	float3 ambient = gMatKa * albedo + gAmbientK * albedo;
	float3 diffuse = NdotL * albedo * gLightColor;

	float3 V = normalize(gEyePosW - pin.PosW);
	float3 H = normalize(L + V);
	float specNorm = saturate(gMatNs / 128.0f);
	float shininess = lerp(1.f, 256.f, specNorm * specNorm);
	float specAmt = pow(max(dot(N, H), 0.f), shininess) * NdotL;
	float3 specular = specAmt * gMatKs * gLightColor;

	return float4(ambient + diffuse + specular, 1.f);
}
