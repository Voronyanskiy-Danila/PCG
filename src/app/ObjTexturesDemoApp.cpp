// =============================================================================
// ObjTexturesDemoApp.cpp — инициализация Lab 3, PSO tessellation, Update, ввод
// =============================================================================
//
// Initialize: LoadModelAndTextures → root sig (3 SRV) → RenderingSystem (compile
//   deferred_tessellation.hlsl) → BuildDeferredGeometryPipeline (VS+HS+DS+PS, PATCH).
// Update: камера, матрицы, EyePosW + DebugMode в mSharedConstants.
// MsgProc: T — mTessDebugMode 0..3 (heatmap / wire / no disp).
// =============================================================================

#include "ObjTexturesDemoApp.h"

#include "../math/MathUtils.h"
#include "../math/SceneFit.h"
#include "../rendering/GBuffer.h"
#include "../rendering/d3d12/D3d12_GpuUploadBuffer.h"
#include "../rendering/d3d12/D3d12_RenderHelpers.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
				   PSTR cmdLine, int showCmd)
{
	(void)prevInstance;
	(void)cmdLine;
	(void)showCmd;

#if defined(DEBUG) || defined(_DEBUG)
	_CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

	wchar_t modulePath[MAX_PATH] = {};
	if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH))
	{
		std::filesystem::path p(modulePath);
		std::error_code ec;
		std::filesystem::current_path(p.parent_path(), ec);
	}


	try
	{
		ObjTexturesDemoApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

ObjTexturesDemoApp::ObjTexturesDemoApp(HINSTANCE hInstance)
: D3d12AppBase(hInstance)
{
	mMainWndCaption = L"PCG Lab4";
}

ObjTexturesDemoApp::~ObjTexturesDemoApp()
{
}

bool ObjTexturesDemoApp::Initialize()
{
	if (!D3d12AppBase::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	LoadModelAndTextures();

	BuildRootSignature();
	BuildGeometryInputLayout();

	mRenderer.Initialize(md3dDevice.Get(), mBackBufferFormat);
	mRenderer.ResizeGBuffer(md3dDevice.Get(), static_cast<UINT>(mClientWidth), static_cast<UINT>(mClientHeight));
	RefreshDeferredSrvs();
	BuildDeferredGeometryPipeline();
	SetupSceneLights();
	UpdateWindowCaption();

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = {mCommandList.Get()};
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	FlushCommandQueue();

	mTextureUploads.clear();

	return true;
}

XMVECTOR ObjTexturesDemoApp::CameraForwardNormalized() const
{
	const float cp = cosf(mPitch);
	const float sp = sinf(mPitch);
	const float cy = cosf(mYaw);
	const float sy = sinf(mYaw);
	XMVECTOR v = XMVectorSet(sy * cp, sp, cy * cp, 0.f);
	return XMVector3Normalize(v);
}

void ObjTexturesDemoApp::OnResize()
{
	D3d12AppBase::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathUtils::Pi, AspectRatio(), 0.5f, 5000.0f);
	XMStoreFloat4x4(&mProj, P);

	mRenderer.ResizeGBuffer(md3dDevice.Get(), static_cast<UINT>(mClientWidth), static_cast<UINT>(mClientHeight));
	if (mSrvHeap)
		RefreshDeferredSrvs();
}

// Каждый кадр: камера + матрицы + ObjectConstants → GPU (hull LOD, domain disp, PS debug)
void ObjTexturesDemoApp::Update(const FrameTimer& gt)
{
	const float dt = gt.DeltaTime();                       // время кадра, сек
	const float speed = mCameraSpeed * dt;                 // метры за кадр

	XMVECTOR forward = CameraForwardNormalized();          // куда смотрит камера

	static const XMVECTOR kWorldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMVECTOR right = XMVector3Cross(kWorldUp, forward);    // вектор «вправо» для A/D
	const float rightLenSq = XMVectorGetX(XMVector3LengthSq(right));
	if (rightLenSq < 1e-8f)
		right = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);      // защита при взгляде вверх/вниз
	else
		right = XMVector3Normalize(right);

	XMVECTOR pos = XMLoadFloat3(&mCameraPos);

	if (mKeyW)
		pos = XMVectorAdd(pos, XMVectorScale(forward, speed));
	if (mKeyS)
		pos = XMVectorSubtract(pos, XMVectorScale(forward, speed));
	if (mKeyA)
		pos = XMVectorSubtract(pos, XMVectorScale(right, speed));
	if (mKeyD)
		pos = XMVectorAdd(pos, XMVectorScale(right, speed));
	if (mKeyAscend)
		pos = XMVectorAdd(pos, XMVectorScale(kWorldUp, speed));
	if (mKeyDescend)
		pos = XMVectorSubtract(pos, XMVectorScale(kWorldUp, speed));

	XMStoreFloat3(&mCameraPos, pos);                       // новая позиция → gEyePosW в шейдере

	const XMVECTOR lookAt = XMVectorAdd(pos, forward);
	const XMMATRIX view = XMMatrixLookAtLH(pos, lookAt, kWorldUp);
	XMStoreFloat4x4(&mView, view);

	const XMMATRIX proj = XMLoadFloat4x4(&mProj);
	mFrustum.ExtractFromMatrix(view * proj);

	ObjectConstants obj{};
	obj.EyePosW = mCameraPos;
	obj.UvScale = {1.0f, 1.0f};
	obj.TessNear = 35.0f;
	obj.TessFar = 180.0f;
	obj.DebugMode = static_cast<float>(mTessDebugMode);
	// DispScale, MinTess, MaxTess, TessNear, TessFar — defaults из struct в .h
	mSharedConstants = obj;

	mDisplayFps = (dt > 1e-6f) ? (1.0f / dt) : 0.0f;

	UpdateVisibility();
	UpdateWindowCaption();

	UpdateCameraAttachedSpotLight();
	mRenderer.SetLights(mSceneLights.data(), static_cast<UINT>(mSceneLights.size()));
}

void ObjTexturesDemoApp::UpdateVisibility()
{
	mVisibleInstances.clear();

	if (!mFrustumCullingEnabled)
	{
		mVisibleInstances.reserve(mInstances.size());
		for (uint32_t i = 0; i < static_cast<uint32_t>(mInstances.size()); ++i)
			mVisibleInstances.push_back(i);
	}
	else
	{
		const XMMATRIX view = XMLoadFloat4x4(&mView);
		const XMMATRIX proj = XMLoadFloat4x4(&mProj);

		if (mOctreeFrustumEnabled)
		{
			mOctree.QueryFrustum(
				mFrustum,
				mOctreeItems,
				mMeshLocalBounds,
				view,
				proj,
				mInstances.data(),
				sizeof(SceneInstance),
				offsetof(SceneInstance, World),
				static_cast<uint32_t>(mInstances.size()),
				mVisibleInstances);
		}
		else
			CullInstancesLinear(view, proj);
	}

	mVisibleCount = static_cast<UINT>(mVisibleInstances.size());
}

void ObjTexturesDemoApp::CullInstancesLinear(const XMMATRIX& view, const XMMATRIX& proj)
{
	mVisibleInstances.reserve(mInstances.size());
	for (uint32_t i = 0; i < static_cast<uint32_t>(mInstances.size()); ++i)
	{
		const XMMATRIX wvp = XMLoadFloat4x4(&mInstances[i].World) * view * proj;
		const XMMATRIX clipRow = XMMatrixTranspose(wvp);
		if (mFrustum.IntersectsAabb(mMeshLocalBounds, clipRow))
			mVisibleInstances.push_back(i);
	}
}

void ObjTexturesDemoApp::UpdateCameraAttachedSpotLight()
{
	if (mSceneLights.size() < 2u || mSceneLights[1].Type != kLightTypeSpot)
		return;

	XMVECTOR forward = CameraForwardNormalized();

	GpuLight& spot = mSceneLights[1];
	spot.Position = mCameraPos;
	XMStoreFloat3(&spot.Direction, forward);
}

void ObjTexturesDemoApp::OnMouseDown(WPARAM /*btnState*/, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void ObjTexturesDemoApp::OnMouseUp(WPARAM /*btnState*/, int /*x*/, int /*y*/)
{
    ReleaseCapture();
}

void ObjTexturesDemoApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & (MK_RBUTTON | MK_LBUTTON)) != 0)
	{
		const float dx = mMouseSensitivity * static_cast<float>(x - mLastMousePos.x);
		const float dy = mMouseSensitivity * static_cast<float>(y - mLastMousePos.y);

		mYaw += dx;
		mPitch -= dy;
		const float pitchLimit = XM_PIDIV2 - 0.05f;
		mPitch = MathUtils::Clamp(mPitch, -pitchLimit, pitchLimit);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void ObjTexturesDemoApp::BuildDescriptorHeaps(UINT srvCount)
{
	D3D12_DESCRIPTOR_HEAP_DESC dh{};
	dh.NumDescriptors = srvCount;
	dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	dh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	dh.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&mSrvHeap)));
}

void ObjTexturesDemoApp::BuildConstantBuffers()
{
	static constexpr UINT kMaxInstances = 1024;
	mObjectCbElementSize = Dx12Utils::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	mObjectCB = std::make_unique<GpuUploadBuffer<ObjectConstants>>(md3dDevice.Get(), kMaxInstances, true);
}

// Root signature геометрического прохода Lab 3: b0 = ObjectCB, t0..t2 = diffuse/normal/disp
void ObjTexturesDemoApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE srvTable{};
	srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[2]{};
	slotRootParameter[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
	slotRootParameter[1].InitAsDescriptorTable(1, &srvTable, D3D12_SHADER_VISIBILITY_ALL);

	CD3DX12_STATIC_SAMPLER_DESC samplerDesc(
		0,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		0.f,
		16,
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
		0.f,
		D3D12_FLOAT32_MAX,
		D3D12_SHADER_VISIBILITY_ALL,
		0);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		2,
		slotRootParameter,
		1,
		&samplerDesc,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(
		&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(),
		errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&mRootSignature)));
}

void ObjTexturesDemoApp::BuildGeometryInputLayout()
{
	mInputLayout = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
}

void ObjTexturesDemoApp::CreateSrvForTexture(int heapIndex, ID3D12Resource* tex)
{
	D3D12_RESOURCE_DESC desc = tex->GetDesc();
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.f;

	D3D12_CPU_DESCRIPTOR_HANDLE h = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(heapIndex) * mCbvSrvUavDescriptorSize;
	md3dDevice->CreateShaderResourceView(tex, &srvDesc, h);
}

void ObjTexturesDemoApp::BuildModelGeometry(const ObjMeshData& data)
{
	const size_t n = data.Positions.size();
	std::vector<Vertex> verts(n);
	for (size_t i = 0; i < n; ++i)
	{
		verts[i].Pos = data.Positions[i];
		verts[i].Normal = data.Normals[i];
		verts[i].TexC = data.Texcoords[i];
	}

	const UINT vbByteSize = static_cast<UINT>(sizeof(Vertex) * verts.size());
	const UINT ibByteSize = static_cast<UINT>(sizeof(uint32_t) * data.Indices32.size());

	mModelGeo = std::make_unique<MeshGeometry>();
	mModelGeo->Name = "ObjModel";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &mModelGeo->VertexBufferCPU));
	CopyMemory(mModelGeo->VertexBufferCPU->GetBufferPointer(), verts.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &mModelGeo->IndexBufferCPU));
	CopyMemory(mModelGeo->IndexBufferCPU->GetBufferPointer(), data.Indices32.data(), ibByteSize);

	mModelGeo->VertexBufferGPU = Dx12Utils::CreateDefaultBuffer(
		md3dDevice.Get(),
		mCommandList.Get(),
		verts.data(),
		vbByteSize,
		mModelGeo->VertexBufferUploader);

	mModelGeo->IndexBufferGPU = Dx12Utils::CreateDefaultBuffer(
		md3dDevice.Get(),
		mCommandList.Get(),
		data.Indices32.data(),
		ibByteSize,
		mModelGeo->IndexBufferUploader);

	mModelGeo->VertexByteStride = sizeof(Vertex);
	mModelGeo->VertexBufferByteSize = vbByteSize;
	mModelGeo->IndexFormat = DXGI_FORMAT_R32_UINT;
	mModelGeo->IndexBufferByteSize = ibByteSize;

	mModelGeo->IndexCount = static_cast<UINT>(data.Indices32.size());
	mModelGeo->StartIndexLocation = 0;
	mModelGeo->BaseVertexLocation = 0;
}

// PSO с hull/domain — заменяет Lab2 deferred_gbuffer.hlsl (простой VS+PS)
void ObjTexturesDemoApp::BuildDeferredGeometryPipeline()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.InputLayout = {mInputLayout.data(), (UINT)mInputLayout.size()};
	psoDesc.pRootSignature = mRootSignature.Get();

	if (!mRenderer.TessVsByteCode() || !mRenderer.TessHsByteCode() || !mRenderer.TessDsByteCode() ||
		!mRenderer.TessPsByteCode())
		throw DxException(E_FAIL,
			L"Tessellation geometry shaders are not initialized.",
			AnsiToWString(__FILE__),
			__LINE__);

	psoDesc.VS = {
		reinterpret_cast<BYTE*>(mRenderer.TessVsByteCode()->GetBufferPointer()),
		mRenderer.TessVsByteCode()->GetBufferSize()};
	psoDesc.HS = {
		reinterpret_cast<BYTE*>(mRenderer.TessHsByteCode()->GetBufferPointer()),
		mRenderer.TessHsByteCode()->GetBufferSize()};
	psoDesc.DS = {
		reinterpret_cast<BYTE*>(mRenderer.TessDsByteCode()->GetBufferPointer()),
		mRenderer.TessDsByteCode()->GetBufferSize()};
	psoDesc.PS = {
		reinterpret_cast<BYTE*>(mRenderer.TessPsByteCode()->GetBufferPointer()),
		mRenderer.TessPsByteCode()->GetBufferSize()};

	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
	psoDesc.NumRenderTargets = GBuffer::kRtCount;
	for (UINT i = 0; i < GBuffer::kRtCount; ++i)
		psoDesc.RTVFormats[i] = GBuffer::RtFormat(i);
	for (UINT i = GBuffer::kRtCount; i < 8; ++i)
		psoDesc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mDeferredGeoPSO)));

	CD3DX12_RASTERIZER_DESC wireRs(D3D12_DEFAULT);
	wireRs.FillMode = D3D12_FILL_MODE_WIREFRAME;
	wireRs.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.RasterizerState = wireRs;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mDeferredGeoWirePSO)));
}

void ObjTexturesDemoApp::UpdateWindowCaption()
{
	wchar_t cap[128];
	swprintf_s(cap, 128, L"%.0f FPS | %u/%u", mDisplayFps, mVisibleCount, mInstanceCount);
	SetWindowText(mhMainWnd, cap);
}

void ObjTexturesDemoApp::RefreshDeferredSrvs()
{
	if (!mSrvHeap)
		return;
	mRenderer.CreateDeferredSrvs(
		md3dDevice.Get(),
		mDeferredSrvHeapBase,
		mCbvSrvUavDescriptorSize,
		mSrvHeap.Get());
}

void ObjTexturesDemoApp::SetupSceneLights()
{
	mSceneLights.clear();

	const float keyLightIntensity = 5.25f;

	GpuLight pt{};
	pt.Type = kLightTypePoint;
	pt.Position = XMFLOAT3(0.f, 60.f, 0.f);
	pt.Range = 400.f;
	pt.Color = XMFLOAT3(1.f, 0.95f, 0.88f);
	pt.Intensity = keyLightIntensity * 0.35f;
	pt.Direction = XMFLOAT3(0.f, -1.f, 0.f);
	pt.SpotInnerCos = 0.f;
	pt.SpotOuterCos = 0.f;
	pt.Padding = XMFLOAT2(0.f, 0.f);
	mSceneLights.push_back(pt);

	GpuLight sp{};
	sp.Type = kLightTypeSpot;
	sp.Position = mCameraPos;
	sp.Direction = XMFLOAT3(0.f, 0.f, 1.f);
	sp.Range = 120.f;
	sp.Color = XMFLOAT3(0.85f, 0.92f, 1.f);
	sp.Intensity = keyLightIntensity;
	sp.SpotInnerCos = cosf(XMConvertToRadians(8.f));
	sp.SpotOuterCos = cosf(XMConvertToRadians(17.f));
	sp.Padding = XMFLOAT2(0.f, 0.f);
	mSceneLights.push_back(sp);

	UpdateCameraAttachedSpotLight();
	mRenderer.SetLights(mSceneLights.data(), static_cast<UINT>(mSceneLights.size()));
}

LRESULT ObjTexturesDemoApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_KEYDOWN:
		switch (wParam)
		{
		case 'W':
		case 'w':
			mKeyW = true;
			return 0;
		case 'A':
		case 'a':
			mKeyA = true;
			return 0;
		case 'S':
		case 's':
			mKeyS = true;
			return 0;
		case 'D':
		case 'd':
			mKeyD = true;
			return 0;
		case VK_SPACE:
			mKeyAscend = true;
			return 0;
		case VK_LCONTROL:
		case VK_RCONTROL:
			mKeyDescend = true;
			return 0;
		// Lab 3 debug: 0 normal, 1 tess heatmap, 2 wireframe PSO, 3 tess без displacement
		case 'T':
		case 't':
			if ((lParam & 0x40000000) == 0)
				mTessDebugMode = (mTessDebugMode + 1) % 4;
			return 0;
		case VK_F1:
			if ((lParam & 0x40000000) == 0)
			{
				mFrustumCullingEnabled = !mFrustumCullingEnabled;
				UpdateWindowCaption();
			}
			return 0;
		case VK_F3:
			if ((lParam & 0x40000000) == 0)
			{
				mOctreeFrustumEnabled = !mOctreeFrustumEnabled;
				UpdateWindowCaption();
			}
			return 0;
		}
		break;
	case WM_KEYUP:
		switch (wParam)
		{
		case VK_F1:
		case VK_F2:
		case VK_F3:
			return 0;
		case 'W':
		case 'w':
			mKeyW = false;
			return 0;
		case 'A':
		case 'a':
			mKeyA = false;
			return 0;
		case 'S':
		case 's':
			mKeyS = false;
			return 0;
		case 'D':
		case 'd':
			mKeyD = false;
			return 0;
		case VK_SPACE:
			mKeyAscend = false;
			return 0;
		case VK_LCONTROL:
		case VK_RCONTROL:
			mKeyDescend = false;
			return 0;
		}
		break;
	}

	return D3d12AppBase::MsgProc(hwnd, msg, wParam, lParam);
}