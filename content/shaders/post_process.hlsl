// post_process.hlsl — Lab 7: fullscreen vignette + chromatic aberration + grayscale

Texture2D gSceneColor : register(t0);              // вход: уже отрендеренная сцена (lighting + particles)
SamplerState gLinearClamp : register(s0);          // линейная фильтрация, UV clamp к краям [0,1]

cbuffer PostCB : register(b0)                      // constant buffer b0 — параметры эффектов с CPU
{
	float gVignetteStrength;                         // сила затемнения к краям (больше = темнее углы)
	float gVignettePower;                            // степень кривой виньетки (pow после saturate)
	float gChromaticStrength;                        // базовое смещение UV для R/B каналов
	float gChromaticRadial;                          // экспонента радиального роста аберрации к краям
	float gPad0;                                     // padding до 16 байт (выравнивание HLSL cbuffer)
	float3 gPad1;                                    // padding float3 — итого 48 байт PostProcessConstants
};

struct VSOut                                      // выход vertex shader → input pixel shader
{
	float4 PosH : SV_POSITION;                       // позиция вершины в clip space (-1..1)
	float2 TexC : TEXCOORD0;                         // UV координаты fullscreen quad (0..1)
};

VSOut VS_Post(uint vid : SV_VertexID)              // fullscreen triangle: 3 вершины без vertex buffer
{
	VSOut o;                                         // локальная структура выхода
	float2 full = float2((vid << 1u) & 2u, vid & 2u); // из vid (0,1,2) получаем углы (0,0),(2,0),(0,2)
	o.PosH = float4(full.x * 2.f - 1.f, -full.y * 2.f + 1.f, 0.f, 1.f); // NDC: x,y в [-1,1], z=0, w=1
	o.TexC = float2(full.x * 0.5f, full.y * 0.5f);   // UV: (0,0), (1,0), (0,1) для треугольника
	return o;                                        // вернуть вершину fullscreen pass
}

float2 ScreenUv(float4 posSs, uint w, uint h)      // pixel coord (SV_Position) → UV [0,1]
{
	return posSs.xy / float2(max(w, 1u), max(h, 1u)); // делим xy на размер текстуры (защита w/h>=1)
}

// Post 1: затемнение к краям экрана (виньетка)
float4 PS_Vignette(VSOut pin, float4 posSs : SV_Position) : SV_TARGET // PS: sceneColor → RT с виньеткой
{
	uint w, h, levels;                               // размеры gSceneColor (levels не используется)
	gSceneColor.GetDimensions(0, w, h, levels);    // читаем width/height текстуры сцены
	float2 uv = ScreenUv(posSs, w, h);               // UV текущего пикселя

	float4 c = gSceneColor.Sample(gLinearClamp, uv); // исходный цвет пикселя сцены
	float2 d = uv - 0.5;                             // смещение от центра экрана (0.5, 0.5)
	float vig = 1.0 - dot(d, d) * gVignetteStrength; // чем дальше от центра, тем меньше vig
	vig = pow(saturate(vig), gVignettePower);        // clamp [0,1] и смягчение/усиление кривой
	return float4(c.rgb * vig, c.a);                 // умножаем RGB на vig, alpha без изменений
}

// Post 2: хроматическая аберрация — R/B смещаются от центра (имитация линзы)
float4 PS_ChromaticAberration(VSOut pin, float4 posSs : SV_Position) : SV_TARGET // PS: R/B shift → RT
{
	uint w, h, levels;                               // размеры входной текстуры
	gSceneColor.GetDimensions(0, w, h, levels);    // width/height для ScreenUv
	float2 uv = ScreenUv(posSs, w, h);               // UV текущего пикселя

	float2 toCenter = uv - 0.5;                      // вектор от центра к пикселю
	float radial = pow(length(toCenter) * 2.0, gChromaticRadial); // 0 в центре, растёт к краям
	float2 dir = normalize(toCenter + 1e-5) * (gChromaticStrength * radial); // направление и величина сдвига UV

	float r = gSceneColor.Sample(gLinearClamp, uv - dir).r; // красный: sample левее/к центру
	float g = gSceneColor.Sample(gLinearClamp, uv).g;       // зелёный: без смещения (опорный канал)
	float b = gSceneColor.Sample(gLinearClamp, uv + dir).b; // синий: sample правее/от центра
	float a = gSceneColor.Sample(gLinearClamp, uv).a;       // alpha из центрального sample
	return float4(r, g, b, a);                       // собранный RGB с расслоением по краям
}

// Post 3: чёрно-белый фильтр (luminance по Rec. 709)
float4 PS_Grayscale(VSOut pin, float4 posSs : SV_Position) : SV_TARGET
{
	uint w, h, levels;
	gSceneColor.GetDimensions(0, w, h, levels);
	float2 uv = ScreenUv(posSs, w, h);

	float4 c = gSceneColor.Sample(gLinearClamp, uv);
	float luma = dot(c.rgb, float3(0.299, 0.587, 0.114));
	return float4(luma, luma, luma, c.a);
}
