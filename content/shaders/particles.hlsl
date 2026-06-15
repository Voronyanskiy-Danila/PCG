// particles.hlsl — Lab 5: compute (Append/Consume) + GS billboards
// Lab 5 доп: круглые партиклы через 2D SDF в PS (мягкий край через fwidth)

cbuffer ParticleSimCB : register(b0)
{
	float gDeltaTime;
	float gGravity;
	uint gMaxParticles;
	uint gSpawnCount;
	float3 gEmitterPos;
	float gPadSim0;
};

cbuffer ParticleDrawCB : register(b1)
{
	float4x4 gViewProj;
	float3 gCameraRight;
	float gPad0;
	float3 gCameraUp;
	float gPad1;
};

struct Particle
{
	float3 Pos;
	float Life;
	float3 Vel;
	float Size;
	float4 Color;
};

ConsumeStructuredBuffer<Particle> gConsumeIn : register(u0);
AppendStructuredBuffer<Particle> gAppendOut : register(u1);
StructuredBuffer<Particle> gRenderParticles : register(t0);

struct VSOut
{
	float3 Pos : POSITION;
	float Size : PSIZE;
	float4 Color : COLOR0;
};

struct GSOut
{
	float4 PosH : SV_POSITION;
	float2 Uv : TEXCOORD0;
	float4 Color : COLOR0;
};

[numthreads(64, 1, 1)]
void CS_Update(uint3 dtid : SV_DispatchThreadID)
{
	if (dtid.x >= gMaxParticles)
		return;

	Particle p = gConsumeIn.Consume();
	const bool badLife = (p.Life <= 0.0f || p.Life > 8.0f || !isfinite(p.Life));
	const bool badPos =
		!all(isfinite(p.Pos)) ||
		any(abs(p.Pos - gEmitterPos) > 250.0f);
	const bool needRespawn = badLife || badPos;

	if (!needRespawn)
	{
		p.Vel.y += gGravity * gDeltaTime;
		p.Pos += p.Vel * gDeltaTime;
		p.Life -= gDeltaTime;
	}

	if (needRespawn || p.Life <= 0.0f)
	{
		float a = frac(dtid.x * 0.6180339 + gDeltaTime * 0.1) * 6.2831853;
		float r = 1.5 + sqrt(frac(dtid.x * 0.37)) * 9.0;
		float h = (frac(dtid.x * 0.27) - 0.5) * 10.0;
		p.Pos = gEmitterPos + float3(cos(a) * r, h, sin(a) * r);
		p.Vel = float3(0.0, -(3.0 + frac(dtid.x * 0.13) * 3.5), 0.0);
		p.Size = 0.9 + frac(dtid.x * 0.21) * 0.7;
		p.Color = float4(0.95, 0.88, 0.75, 1.0);
		p.Life = 1.8 + frac(dtid.x * 0.11) * 1.5;
	}

	gAppendOut.Append(p);
}

VSOut VS_Particle(uint id : SV_VertexID)
{
	Particle p = gRenderParticles[id];
	VSOut o;
	o.Pos = p.Pos;
	o.Size = p.Size;
	o.Color = p.Color;
	return o;
}

[maxvertexcount(6)]
void GS_Billboard(point VSOut input[1], inout TriangleStream<GSOut> triStream)
{
	VSOut pin = input[0];
	const float halfSize = pin.Size * 0.5f;

	float3 right = gCameraRight * halfSize;
	float3 up = gCameraUp * halfSize;

	const float3 bl = pin.Pos - right - up;
	const float3 br = pin.Pos + right - up;
	const float3 tl = pin.Pos - right + up;
	const float3 tr = pin.Pos + right + up;

	GSOut o;
	o.Color = pin.Color;

	o.PosH = mul(float4(bl, 1.0f), gViewProj);
	o.Uv = float2(0.0f, 1.0f);
	triStream.Append(o);

	o.PosH = mul(float4(br, 1.0f), gViewProj);
	o.Uv = float2(1.0f, 1.0f);
	triStream.Append(o);

	o.PosH = mul(float4(tl, 1.0f), gViewProj);
	o.Uv = float2(0.0f, 0.0f);
	triStream.Append(o);
	triStream.RestartStrip();

	o.PosH = mul(float4(br, 1.0f), gViewProj);
	o.Uv = float2(1.0f, 1.0f);
	triStream.Append(o);

	o.PosH = mul(float4(tr, 1.0f), gViewProj);
	o.Uv = float2(1.0f, 0.0f);
	triStream.Append(o);

	o.PosH = mul(float4(tl, 1.0f), gViewProj);
	o.Uv = float2(0.0f, 0.0f);
	triStream.Append(o);
	triStream.RestartStrip();
}

// 2D signed distance field: отрицательно внутри круга, положительно снаружи.
float SdCircle(float2 p, float radius)
{
	return length(p) - radius;
}

float4 PS_Particle(GSOut pin) : SV_TARGET
{
	// UV квадрата billboard → локальные координаты, центр (0,0), радиус 1.
	const float2 p = pin.Uv * 2.0f - 1.0f;
	const float sdf = SdCircle(p, 1.0f);
	const float aa = max(fwidth(sdf), 1e-4f);
	const float coverage = saturate(0.5f - sdf / aa);

	float4 col = pin.Color;
	const float core = saturate(-sdf * 2.2f);
	col.rgb *= lerp(0.88f, 1.18f, core);
	col.a *= coverage;
	if (col.a <= (1.0f / 255.0f))
		discard;
	return col;
}
