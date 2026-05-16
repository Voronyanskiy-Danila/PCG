#include "ObjTexturesDemoApp.h"

#include "TextureUvSettings.h"

#include "../math/MathUtils.h"
#include "../math/SceneFit.h"
#include "../rendering/GBuffer.h"
#include "../rendering/d3d12/D3d12_GpuUploadBuffer.h"

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
	mMainWndCaption = L"Sponza — D3D12";
}

ObjTexturesDemoApp::~ObjTexturesDemoApp()
{
}

bool ObjTexturesDemoApp::Initialize()
{
	if (!D3d12AppBase::Initialize())
		return false;

	LoadIniTextureUvSettings();

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	LoadModelAndTextures();

	BuildRootSignature();
	BuildGeometryInputLayout();

	mRenderer.Initialize(md3dDevice.Get(), mBackBufferFormat);
	mRenderer.ResizeGBuffer(md3dDevice.Get(), static_cast<UINT>(mClientWidth), static_cast<UINT>(mClientHeight));
	RefreshDeferredSrvs();
	BuildDeferredGeometryPipeline();
	SetupSceneLights();

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = {mCommandList.Get()};
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	FlushCommandQueue();

	mTextureUploads.clear();

	return true;
}

void ObjTexturesDemoApp::LoadIniTextureUvSettings()
{
	TextureUvSettings cfg{};
	cfg.TilingRepeatsX = mUvScale.x;
	cfg.TilingRepeatsY = mUvScale.y;
	cfg.TextureMovementEnabled = mTextureMovementEnabled;

	// Рядом с .exe: ...\bin\x64\Release\content\texture_uv_settings.ini (как Post-Build в vcxproj).
	std::filesystem::path path;
	wchar_t modulePath[MAX_PATH] = {};
	if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH))
		path = std::filesystem::path(modulePath).parent_path() / L"content" / L"texture_uv_settings.ini";
	else
		path = std::filesystem::current_path() / L"content" / L"texture_uv_settings.ini";

	[[maybe_unused]] const bool ok = TextureUvSettings::LoadIni(path.c_str(), cfg);

#if defined(DEBUG) || defined(_DEBUG)
	wchar_t buf[768];
	swprintf_s(
		buf,
		L"[texture_uv_settings.ini]\n path=%s\n ok=%d  tiling=(%.4f, %.4f)  movement=%d\n",
		path.c_str(),
		ok ? 1 : 0,
		cfg.TilingRepeatsX,
		cfg.TilingRepeatsY,
		cfg.TextureMovementEnabled ? 1 : 0);
	OutputDebugStringW(buf);
#endif

	mUvScale.x = cfg.TilingRepeatsX;
	mUvScale.y = cfg.TilingRepeatsY;
	mTextureMovementEnabled = cfg.TextureMovementEnabled;
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

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathUtils::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);

	mRenderer.ResizeGBuffer(md3dDevice.Get(), static_cast<UINT>(mClientWidth), static_cast<UINT>(mClientHeight));
	if (mSrvHeap)
		RefreshDeferredSrvs();
}

void ObjTexturesDemoApp::Update(const FrameTimer& gt)
{
	const float dt = gt.DeltaTime();
	const float speed = mCameraSpeed * dt;

	XMVECTOR forward = CameraForwardNormalized();

	static const XMVECTOR kWorldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMVECTOR right = XMVector3Cross(kWorldUp, forward);
	const float rightLenSq = XMVectorGetX(XMVector3LengthSq(right));
	if (rightLenSq < 1e-8f)
		right = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
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

	XMStoreFloat3(&mCameraPos, pos);

	const XMVECTOR lookAt = XMVectorAdd(pos, forward);
	const XMMATRIX view = XMMatrixLookAtLH(pos, lookAt, kWorldUp);
	XMStoreFloat4x4(&mView, view);

	XMMATRIX world = XMLoadFloat4x4(&mWorld);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);
	XMMATRIX wvp = world * view * proj;

	ObjectConstants obj{};

	// Матрицы: в HLSL row-vector * matrix (mul(v, M)) — передаём транспонированные.
	XMStoreFloat4x4(&obj.World, XMMatrixTranspose(world));
	XMStoreFloat4x4(&obj.WorldInvTranspose, XMMatrixTranspose(MathUtils::InverseTranspose(world)));
	XMStoreFloat4x4(&obj.WorldViewProj, XMMatrixTranspose(wvp));

	obj.EyePosW = mCameraPos;
	obj.SpecPower = 64.0f;

	obj.LightDirW = XMFLOAT3(0.577f, -0.577f, 0.577f);
	obj.LightColor = XMFLOAT3(1.0f, 1.0f, 1.0f);
	obj.AmbientK = 0.15f;

	obj.UvAnimParams =
		XMFLOAT4(gt.TotalTime(), mTextureMovementEnabled ? 1.f : 0.f, 0.f, 0.f);
	obj.UvScale = mUvScale;
	obj.UvScroll = mUvScroll;

	mSharedConstants = obj;

	UpdateCameraAttachedSpotLight();
	mRenderer.SetLights(mSceneLights.data(), static_cast<UINT>(mSceneLights.size()));
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
	mObjectCB = std::make_unique<GpuUploadBuffer<ObjectConstants>>(md3dDevice.Get(), 1, true);
}

void ObjTexturesDemoApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE srvTable{};
	srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[2]{};
	slotRootParameter[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
	slotRootParameter[1].InitAsDescriptorTable(1, &srvTable, D3D12_SHADER_VISIBILITY_PIXEL);

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
		D3D12_SHADER_VISIBILITY_PIXEL,
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

void ObjTexturesDemoApp::FitWorldAndCameraToMesh(const ObjMeshData& data)
{
	const SceneFitResult fit = ComputeSceneFit(data);
	mWorld = fit.World;
	mCameraPos = fit.CameraPos;
	mYaw = fit.CameraYaw;
	mPitch = fit.CameraPitch;
}

void ObjTexturesDemoApp::BuildDeferredGeometryPipeline()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.InputLayout = {mInputLayout.data(), (UINT)mInputLayout.size()};
	psoDesc.pRootSignature = mRootSignature.Get();

	if (!mRenderer.GeomVsByteCode() || !mRenderer.GeomPsByteCode())
		throw DxException(E_FAIL,
			L"Deferred geometry shaders are not initialized.",
			AnsiToWString(__FILE__),
			__LINE__);

	psoDesc.VS = {
		reinterpret_cast<BYTE*>(mRenderer.GeomVsByteCode()->GetBufferPointer()),
		mRenderer.GeomVsByteCode()->GetBufferSize()};
	psoDesc.PS = {
		reinterpret_cast<BYTE*>(mRenderer.GeomPsByteCode()->GetBufferPointer()),
		mRenderer.GeomPsByteCode()->GetBufferSize()};

	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = GBuffer::kRtCount;
	for (UINT i = 0; i < GBuffer::kRtCount; ++i)
		psoDesc.RTVFormats[i] = GBuffer::RtFormat(i);
	for (UINT i = GBuffer::kRtCount; i < 8; ++i)
		psoDesc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mDeferredGeoPSO)));
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
	// Зелёный point в центре сцены (центр спонзы после FitWorld ~ (0,0,0)); spot с камеры — UpdateCameraAttachedSpotLight.
	mSceneLights.clear();

	const float keyLightIntensity = 5.25f;

	GpuLight pt{};
	pt.Type = kLightTypePoint;
	pt.Position = XMFLOAT3(0.f, 0.f, 0.f);
	pt.Range = 36.f;
	pt.Color = XMFLOAT3(0.12f, 1.f, 0.32f);
	pt.Intensity = keyLightIntensity * 0.5f;
	pt.Direction = XMFLOAT3(0.f, -1.f, 0.f);
	pt.SpotInnerCos = 0.f;
	pt.SpotOuterCos = 0.f;
	pt.Padding = XMFLOAT2(0.f, 0.f);
	mSceneLights.push_back(pt);

	GpuLight sp{};
	sp.Type = kLightTypeSpot;
	sp.Position = mCameraPos;
	sp.Direction = XMFLOAT3(0.f, 0.f, 1.f);
	sp.Range = 32.f;
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
		}
		break;
	case WM_KEYUP:
		switch (wParam)
		{
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