// particles.hlsl — Lab 5: compute (Append/Consume) + GS billboards

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

[maxvertexcount(4)]
void GS_Billboard(point VSOut input[1], inout TriangleStream<GSOut> triStream)
{
	VSOut pin = input[0];
	const float halfSize = pin.Size * 0.5f;

	float3 right = gCameraRight * halfSize;
	float3 up = gCameraUp * halfSize;

	float3 corners[4] = {
		pin.Pos - right - up,
		pin.Pos + right - up,
		pin.Pos + right + up,
		pin.Pos - right + up
	};

	float2 uvs[4] = {
		float2(0.0f, 1.0f),
		float2(1.0f, 1.0f),
		float2(1.0f, 0.0f),
		float2(0.0f, 0.0f)
	};

	[unroll]
	for (uint i = 0; i < 4u; ++i)
	{
		GSOut o;
		o.PosH = mul(float4(corners[i], 1.0f), gViewProj);
		o.Uv = uvs[i];
		o.Color = pin.Color;
		triStream.Append(o);
	}

	triStream.RestartStrip();
}

float4 PS_Particle(GSOut pin) : SV_TARGET
{
	return pin.Color;
}
