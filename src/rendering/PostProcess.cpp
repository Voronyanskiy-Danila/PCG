// PostProcess.cpp — Lab 7: CPU-часть post-process (буферы, PSO, Run каждый кадр)

#include "PostProcess.h"                                      // класс PostProcess и PostProcessConstants

#include "../rendering/d3d12/D3d12_RenderHelpers.h"           // CompileShader, ThrowIfFailed

#include <vector>

using Microsoft::WRL::ComPtr;                                 // умные указатели COM (ID3D12*)
using namespace DirectX;                                        // XMFLOAT и т.д. (если понадобится)

namespace                                                       // локальные helper'ы только для этого .cpp
{

// Создаёт SRV для 2D color-текстуры (чтение gSceneColor в post_process.hlsl)
void CreateColorSrv(
	ID3D12Device* device,                                       // D3D12 device
	ID3D12Resource* tex,                                        // текстура sceneColor или tempColor
	DXGI_FORMAT srvFormat,                                      // формат SRV (обычно R8G8B8A8_UNORM)
	D3D12_CPU_DESCRIPTOR_HANDLE dst)                            // куда записать descriptor
{
	D3D12_SHADER_RESOURCE_VIEW_DESC d{};                        // описание SRV
	d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; // RGBA без swizzle
	d.Format = srvFormat;                                       // формат sample в shader
	d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;            // обычная 2D текстура
	d.Texture2D.MipLevels = 1u;                                 // один mip (fullscreen RT)
	d.Texture2D.MostDetailedMip = 0u;                           // mip 0
	d.Texture2D.ResourceMinLODClamp = 0.f;                      // min LOD clamp
	device->CreateShaderResourceView(tex, &d, dst);             // создать SRV в dst
}

// Один fullscreen pass: triangle 3 vert без VB, как deferred lighting
void DrawFullscreen(
	ID3D12GraphicsCommandList* cmd,                             // command list кадра
	ID3D12PipelineState* pso,                                   // vignette или chromatic PSO
	ID3D12RootSignature* rs,                                    // root: b0 CBV + t0 SRV
	D3D12_GPU_VIRTUAL_ADDRESS cbGpu,                            // GPU адрес PostProcessConstants
	CD3DX12_GPU_DESCRIPTOR_HANDLE sceneSrvGpu)                  // GPU handle SRV входной текстуры
{
	cmd->SetPipelineState(pso);                                 // bind graphics PSO
	cmd->SetGraphicsRootSignature(rs);                          // bind root signature
	cmd->SetGraphicsRootConstantBufferView(0u, cbGpu);          // b0 — параметры виньетки/хроматики
	cmd->SetGraphicsRootDescriptorTable(1u, sceneSrvGpu);       // t0 — gSceneColor
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // треугольник
	cmd->IASetVertexBuffers(0u, 0u, nullptr);                   // vertex buffer не нужен (procedural VS)
	cmd->DrawInstanced(3u, 1u, 0u, 0u);                         // 3 вершины = fullscreen triangle
}

} // namespace

// Старт: формат RT, CB, компиляция шейдеров, создание PSO (один раз при Init RenderingSystem)
void PostProcess::Initialize(ID3D12Device* device, DXGI_FORMAT rtFormat)
{
	m_rtFormat = rtFormat;                                      // формат scene/temp RT = формат swap chain
	m_constantsCb = std::make_unique<GpuUploadBuffer<PostProcessConstants>>(device, 1u, true); // upload CB

	m_vsBc = Dx12Utils::CompileShader(                          // VS_Post — fullscreen triangle
		L"content/shaders/post_process.hlsl", nullptr, "VS_Post", "vs_5_0");
	m_psVignetteBc = Dx12Utils::CompileShader(                  // PS_Vignette — затемнение к краям
		L"content/shaders/post_process.hlsl", nullptr, "PS_Vignette", "ps_5_0");
	m_psChromaticBc = Dx12Utils::CompileShader(                 // PS_ChromaticAberration — R/B shift
		L"content/shaders/post_process.hlsl", nullptr, "PS_ChromaticAberration", "ps_5_0");
	m_psGrayscaleBc = Dx12Utils::CompileShader(
		L"content/shaders/post_process.hlsl", nullptr, "PS_Grayscale", "ps_5_0");

	BuildPipelines(device);                                     // root signature + post PSOs
}

// При изменении размера окна — пересоздать sceneColor и tempColor
void PostProcess::Resize(ID3D12Device* device, UINT width, UINT height)
{
	m_width = width;                                            // ширина RT
	m_height = height;                                          // высота RT
	if (!device || width == 0u || height == 0u)                 // некорректный resize — выход
		return;

	m_sceneColor.Reset();                                       // освободить старую текстуру сцены
	m_tempColor.Reset();                                        // освободить промежуточный буфер
	m_rtvHeap.Reset();                                          // освободить RTV heap
	m_sceneState = D3D12_RESOURCE_STATE_RENDER_TARGET;          // сброс tracked state scene
	m_tempState = D3D12_RESOURCE_STATE_RENDER_TARGET;           // сброс tracked state temp
	CreateTargets(device);                                      // создать текстуры + RTV + локальные SRV
}

// Resource barrier с отслеживанием текущего state (RT ↔ shader read)
void PostProcess::TransitionResource(
	ID3D12GraphicsCommandList* cmd,                             // command list
	ID3D12Resource* res,                                        // текстура для transition
	D3D12_RESOURCE_STATES& trackedState,                        // in/out: текущее состояние ресурса
	D3D12_RESOURCE_STATES newState)                             // целевое состояние
{
	if (!cmd || !res || trackedState == newState)               // нечего переводить
		return;
	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(res, trackedState, newState); // barrier
	cmd->ResourceBarrier(1u, &barrier);                         // записать barrier в CL
	trackedState = newState;                                    // обновить tracked state
}

// Создание m_sceneColor, m_tempColor, RTV heap, CPU SRV для копирования в общий heap
void PostProcess::CreateTargets(ID3D12Device* device)
{
	m_rtvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV); // шаг RTV

	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};                        // heap для render target views
	rtvDesc.NumDescriptors = 2u;                                // scene + temp
	rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;              // тип RTV
	rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;            // не shader-visible (только OMSetRT)
	ThrowIfFailed(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap))); // создать heap

	CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT); // GPU default heap
	const float clearColor[4] = {0.f, 0.f, 0.f, 0.f};           // чёрный clear
	CD3DX12_CLEAR_VALUE clearValue(m_rtFormat, clearColor);       // clear value для CreateCommittedResource
	CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D( // описание 2D RT текстуры
		m_rtFormat,                                             // формат пикселя
		m_width,                                                // width
		m_height,                                               // height
		1u,                                                     // array size 1
		1u,                                                     // mip 1
		1u,                                                     // sample count
		0u,                                                     // quality
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);               // можно bind как RT

	ThrowIfFailed(device->CreateCommittedResource(              // sceneColor — сюда пишет lighting
		&heapDefault,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,                     // начальное состояние: RT
		&clearValue,
		IID_PPV_ARGS(&m_sceneColor)));
	ThrowIfFailed(device->CreateCommittedResource(              // tempColor — промежуточный post pass
		&heapDefault,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&clearValue,
		IID_PPV_ARGS(&m_tempColor)));

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart(); // RTV[0]
	device->CreateRenderTargetView(m_sceneColor.Get(), nullptr, rtv); // RTV для sceneColor
	rtv.ptr += m_rtvIncrement;                                  // следующий slot в heap
	device->CreateRenderTargetView(m_tempColor.Get(), nullptr, rtv); // RTV для tempColor

	D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};                       // локальный heap SRV (копируем в app heap)
	srvDesc.NumDescriptors = 2u;                                // SRV scene + temp
	srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;      // SRV heap
	srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;            // CPU-only, потом CopyDescriptors
	ComPtr<ID3D12DescriptorHeap> srvHeap;                       // временный heap на время CreateTargets
	ThrowIfFailed(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap)));
	m_sceneSrvCpu = srvHeap->GetCPUDescriptorHandleForHeapStart(); // CPU handle SRV scene
	m_tempSrvCpu = m_sceneSrvCpu;                               // temp = следующий descriptor
	m_tempSrvCpu.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	CreateColorSrv(device, m_sceneColor.Get(), m_rtFormat, m_sceneSrvCpu); // SRV scene
	CreateColorSrv(device, m_tempColor.Get(), m_rtFormat, m_tempSrvCpu);  // SRV temp
}

// RTV для записи сцены (lighting/particles) — первый descriptor в m_rtvHeap
D3D12_CPU_DESCRIPTOR_HANDLE PostProcess::SceneRtv() const
{
	return m_rtvHeap->GetCPUDescriptorHandleForHeapStart();     // m_sceneColor RTV
}

// Копирует SRV post-текстур в shader-visible heap приложения (CreateDeferredSrvs)
void PostProcess::CreateSrvs(
	ID3D12Device* device,
	UINT heapOffsetFirst,                                         // индекс первого post SRV в общем heap
	UINT descriptorIncrementSize,                                 // размер одного descriptor
	ID3D12DescriptorHeap* shaderVisibleSrvHeap)                   // mSrvHeap приложения
{
	if (!device || !shaderVisibleSrvHeap || !m_sceneColor)      // ещё не Resize — выход
		return;

	D3D12_CPU_DESCRIPTOR_HANDLE dst = shaderVisibleSrvHeap->GetCPUDescriptorHandleForHeapStart();
	dst.ptr += static_cast<SIZE_T>(heapOffsetFirst) * descriptorIncrementSize; // slot scene SRV
	device->CopyDescriptorsSimple(1u, dst, m_sceneSrvCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	dst.ptr += descriptorIncrementSize;                         // slot temp SRV
	device->CopyDescriptorsSimple(1u, dst, m_tempSrvCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

// Root signature + два PSO (общий VS, разные PS)
void PostProcess::BuildPipelines(ID3D12Device* device)
{
	CD3DX12_DESCRIPTOR_RANGE srvRange{};                         // диапазон SRV в root table
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 0u);       // 1 SRV, register t0

	CD3DX12_ROOT_PARAMETER rp[2]{};                               // два root parameter
	rp[0].InitAsConstantBufferView(0u, 0u, D3D12_SHADER_VISIBILITY_PIXEL); // b0 PostCB
	rp[1].InitAsDescriptorTable(1u, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL); // table t0

	CD3DX12_STATIC_SAMPLER_DESC sampler{};                        // s0 в root — linear clamp
	sampler.Init(
		0u,                                                     // register s0
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,                        // bilinear
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,                       // U clamp
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,                       // V clamp
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);                      // W clamp

	CD3DX12_ROOT_SIGNATURE_DESC rsd{};                            // описание root signature
	rsd.Init(_countof(rp), rp, 1u, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ComPtr<ID3DBlob> serialized;                                // сериализованный root sig
	ComPtr<ID3DBlob> errors;                                    // ошибки компиляции RS
	ThrowIfFailed(D3D12SerializeRootSignature(
		&rsd,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serialized.GetAddressOf(),
		errors.GetAddressOf()));
	if (errors)                                                 // debug: вывод warnings RS
		OutputDebugStringA(static_cast<char*>(errors->GetBufferPointer()));
	ThrowIfFailed(device->CreateRootSignature(                  // создать m_rootSignature
		0u,
		serialized->GetBufferPointer(),
		serialized->GetBufferSize(),
		IID_PPV_ARGS(&m_rootSignature)));

	auto makePso = [&](ID3DBlob* ps) {                          // lambda: PSO для данного PS blob
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};               // описание graphics PSO
		pso.pRootSignature = m_rootSignature.Get();             // общий root sig
		pso.VS = {m_vsBc->GetBufferPointer(), m_vsBc->GetBufferSize()}; // VS_Post
		pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()}; // PS_Vignette или PS_Chromatic
		CD3DX12_RASTERIZER_DESC rs(D3D12_DEFAULT);              // rasterizer defaults
		rs.CullMode = D3D12_CULL_MODE_NONE;                     // fullscreen tri — cull off
		pso.RasterizerState = rs;
		pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);    // без альфа-бленда
		CD3DX12_DEPTH_STENCIL_DESC ds(D3D12_DEFAULT);           // depth stencil defaults
		ds.DepthEnable = FALSE;                                 // post 2D — depth не нужен
		pso.DepthStencilState = ds;
		pso.SampleMask = UINT_MAX;                              // все samples
		pso.SampleDesc.Count = 1u;                              // MSAA x1
		pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // triangles
		pso.NumRenderTargets = 1u;                              // один color RT
		pso.RTVFormats[0] = m_rtFormat;                         // формат выхода
		ComPtr<ID3D12PipelineState> out;                        // результат PSO
		ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&out)));
		return out;
	};

	m_psoVignette = makePso(m_psVignetteBc.Get());             // PSO виньетки
	m_psoChromatic = makePso(m_psChromaticBc.Get());          // PSO хроматической аберрации
	m_psoGrayscale = makePso(m_psGrayscaleBc.Get());          // PSO чёрно-белого фильтра
}

// Каждый кадр: sceneColor → (temp) → back buffer; вызывается из RenderingSystem::RunPostProcess
void PostProcess::Run(
	ID3D12GraphicsCommandList* cmd,                             // command list
	ID3D12Resource* backBuffer,                                 // swap chain buffer
	D3D12_RESOURCE_STATES backBufferStateBefore,                // state back buffer до post (PRESENT)
	ID3D12DescriptorHeap* srvHeap,                              // shader-visible SRV heap
	UINT srvHeapBaseOffset,                                     // индекс SRV sceneColor в heap
	UINT srvDescriptorIncrement,                                // размер descriptor
	D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,                  // RTV swap chain
	const D3D12_VIEWPORT& viewport,                           // viewport окна
	const D3D12_RECT& scissor,                                  // scissor окна
	bool vignetteEnabled,                                       // F2 — виньетка
	bool chromaticEnabled,                                      // F4 — хроматика
	bool grayscaleEnabled)                                      // F8 — grayscale
{
	if (!cmd || !backBuffer || !m_sceneColor || !m_psoVignette || !m_psoChromatic || !m_psoGrayscale || !m_constantsCb)
		return;                                                 // не готовы ресурсы
	if (!vignetteEnabled && !chromaticEnabled && !grayscaleEnabled)
		return;                                                 // все off — post не нужен

	std::vector<ID3D12PipelineState*> passes;
	passes.reserve(3u);
	if (vignetteEnabled)
		passes.push_back(m_psoVignette.Get());
	if (chromaticEnabled)
		passes.push_back(m_psoChromatic.Get());
	if (grayscaleEnabled)
		passes.push_back(m_psoGrayscale.Get());
	if (passes.empty())
		return;

	PostProcessConstants cb{};                                  // дефолты strength/power из struct
	m_constantsCb->CopyData(0, cb);                             // upload CB на GPU
	const D3D12_GPU_VIRTUAL_ADDRESS cbGpu = m_constantsCb->Resource()->GetGPUVirtualAddress();

	ID3D12DescriptorHeap* heaps[] = {srvHeap};                  // bind SRV heap для post draw
	cmd->SetDescriptorHeaps(1u, heaps);
	cmd->RSSetViewports(1u, &viewport);                        // размер rasterization
	cmd->RSSetScissorRects(1u, &scissor);                       // область отсечения

	const UINT sceneSrvIndex = srvHeapBaseOffset;               // GPU index SRV sceneColor
	const UINT tempSrvIndex = srvHeapBaseOffset + 1u;           // GPU index SRV tempColor
	CD3DX12_GPU_DESCRIPTOR_HANDLE sceneSrvGpu(                  // handle для gSceneColor = scene
		srvHeap->GetGPUDescriptorHandleForHeapStart(),
		static_cast<INT>(sceneSrvIndex),
		srvDescriptorIncrement);
	CD3DX12_GPU_DESCRIPTOR_HANDLE tempSrvGpu(                   // handle для gSceneColor = temp
		srvHeap->GetGPUDescriptorHandleForHeapStart(),
		static_cast<INT>(tempSrvIndex),
		srvDescriptorIncrement);

	D3D12_CPU_DESCRIPTOR_HANDLE tempRtvHandle = SceneRtv();     // RTV temp = второй в heap
	tempRtvHandle.ptr += m_rtvIncrement;
	const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtvHandle = SceneRtv();

	TransitionResource(                                         // scene: RT → readable в PS
		cmd,
		m_sceneColor.Get(),
		m_sceneState,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	D3D12_RESOURCE_STATES backBufferState = backBufferStateBefore; // локальная копия для transition
	TransitionResource(                                         // back buffer: PRESENT → RT
		cmd,
		backBuffer,
		backBufferState,
		D3D12_RESOURCE_STATE_RENDER_TARGET);

	for (size_t i = 0; i < passes.size(); ++i)
	{
		const bool isLast = (i + 1u == passes.size());
		const CD3DX12_GPU_DESCRIPTOR_HANDLE inputSrv = (i % 2u == 0u) ? sceneSrvGpu : tempSrvGpu;

		if (isLast)
		{
			cmd->OMSetRenderTargets(1u, &backBufferRtv, FALSE, nullptr);
		}
		else if (i % 2u == 0u)
		{
			TransitionResource(
				cmd,
				m_tempColor.Get(),
				m_tempState,
				D3D12_RESOURCE_STATE_RENDER_TARGET);
			cmd->OMSetRenderTargets(1u, &tempRtvHandle, FALSE, nullptr);
		}
		else
		{
			TransitionResource(
				cmd,
				m_sceneColor.Get(),
				m_sceneState,
				D3D12_RESOURCE_STATE_RENDER_TARGET);
			cmd->OMSetRenderTargets(1u, &sceneRtvHandle, FALSE, nullptr);
		}

		DrawFullscreen(cmd, passes[i], m_rootSignature.Get(), cbGpu, inputSrv);

		if (!isLast)
		{
			if (i % 2u == 0u)
			{
				TransitionResource(
					cmd,
					m_tempColor.Get(),
					m_tempState,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			}
			else
			{
				TransitionResource(
					cmd,
					m_sceneColor.Get(),
					m_sceneState,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			}
		}
	}

	if (passes.size() > 1u)
	{
		TransitionResource(
			cmd,
			m_tempColor.Get(),
			m_tempState,
			D3D12_RESOURCE_STATE_RENDER_TARGET);
	}

	TransitionResource(                                         // scene снова RT для lighting след. кадра
		cmd,
		m_sceneColor.Get(),
		m_sceneState,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
}
