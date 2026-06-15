#include "ObjTexturesDemoApp.h"

#include "../math/MathUtils.h"
#include "../math/SceneFit.h"
#include "../rendering/GBuffer.h"
#include "../rendering/d3d12/D3d12_GpuUploadBuffer.h"
#include "../rendering/d3d12/D3d12_RenderHelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace
{
std::filesystem::path FindContentRoot(const std::filesystem::path& exeDir)
{
	std::filesystem::path fallback = exeDir;
	std::filesystem::path dir = exeDir;
	for (int i = 0; i < 10; ++i)
	{
		const bool hasContent = std::filesystem::exists(dir / L"content" / L"models");
		const bool hasStuff = std::filesystem::exists(dir / L"Stuff");
		if (hasContent && hasStuff)
			return dir;
		if (hasContent)
			fallback = dir;
		if (!dir.has_parent_path())
			break;
		dir = dir.parent_path();
	}
	return fallback;
}
} // namespace

std::filesystem::path ObjTexturesDemoApp::s_contentRoot = {};

void ObjTexturesDemoApp::SetContentRoot(const std::filesystem::path& root)
{
	s_contentRoot = root;
	std::error_code ec;
	std::filesystem::current_path(s_contentRoot, ec);
}

const std::filesystem::path& ObjTexturesDemoApp::ContentRoot()
{
	return s_contentRoot;
}

std::wstring ObjTexturesDemoApp::ResolveContentPath(const wchar_t* relativePath)
{
	if (!relativePath || relativePath[0] == L'\0')
		return {};
	if (s_contentRoot.empty())
		return relativePath;
	return (s_contentRoot / relativePath).wstring();
}

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
		const std::filesystem::path contentRoot =
			FindContentRoot(std::filesystem::path(modulePath).parent_path());
		ObjTexturesDemoApp::SetContentRoot(contentRoot);
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
	mMainWndCaption = L"PCG Lab 8";
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
	mRenderer.LoadIblTextures(md3dDevice.Get(), mCommandList.Get());
	mRenderer.InitializeParticles(md3dDevice.Get(), 256u);
	mLightRainCircles.Initialize(md3dDevice.Get(), mBackBufferFormat, 512u);
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
	mRenderer.ClearIblUploadHeaps();

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

	XMMATRIX P = XMMatrixPerspectiveFovLH(
		kCameraFovYRad,
		AspectRatio(),
		kCameraNearZ,
		kCameraFarZ);
	XMStoreFloat4x4(&mProj, P);

	mRenderer.ResizeGBuffer(md3dDevice.Get(), static_cast<UINT>(mClientWidth), static_cast<UINT>(mClientHeight));
	mRenderer.ResizeShadows(md3dDevice.Get());
	if (mSrvHeap)
		RefreshDeferredSrvs();
}

// Каждый кадр: камера + матрицы + ObjectConstants → GPU (hull LOD, domain disp, PS debug)
void ObjTexturesDemoApp::Update(const FrameTimer& gt)
{
	const float dt = gt.DeltaTime();                       // время кадра, сек
	const float speedScale = mKeyBoost ? mCameraBoostMultiplier : 1.0f;
	const float speed = mCameraSpeed * speedScale * dt;    // метры за кадр

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
	obj.TessNear = 60.0f;
	obj.TessFar = 400.0f;
	obj.DebugMode = static_cast<float>(mTessDebugMode);
	// DispScale, MinTess, MaxTess, TessNear, TessFar — defaults из struct в .h
	mSharedConstants = obj;
	mVertexAnimTime = gt.TotalTime();
	mSharedConstants.VertexAnimTime = mVertexAnimTime;
	mSharedConstants.PerlinSeed = mPerlinSeed;
	mSharedConstants.PerlinFrequency = 6.0f;

	if (mLightRainEnabled)
		mLightRain.Update(dt);

	mDisplayFps = (dt > 1e-6f) ? (1.0f / dt) : 0.0f;

	UpdateVisibility();
	UpdateShadowCasters();

	UpdateCameraAttachedSpotLight();
	UploadSceneLights();
}

void ObjTexturesDemoApp::UploadSceneLights()
{
	std::vector<GpuLight> lights;
	if (mLightRainEnabled)
		mLightRain.AppendPointLights(lights);
	lights.insert(lights.end(), mSceneLights.begin(), mSceneLights.end());
	mRenderer.SetLights(lights.data(), static_cast<UINT>(lights.size()));
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
	else if (mOctreeFrustumEnabled)
	{
		mOctree.QueryFrustum(mFrustum, mOctreeItems, static_cast<uint32_t>(mInstances.size()), mVisibleInstances);
	}
	else
		CullInstancesLinear();

	mVisibleCount = static_cast<UINT>(mVisibleInstances.size());
}

void ObjTexturesDemoApp::CullInstancesLinear()
{
	mVisibleInstances.reserve(mInstances.size());
	for (uint32_t i = 0; i < static_cast<uint32_t>(mInstances.size()); ++i)
	{
		if (mFrustum.IntersectsAabb(mInstances[i].WorldBounds))
			mVisibleInstances.push_back(i);
	}
}

void ObjTexturesDemoApp::UpdateShadowCasters()
{
	mShadowDrawSponza = true;

	if (!mFrustumCullingEnabled)
		return;

	const XMMATRIX view = XMLoadFloat4x4(&mView);
	const float aspect = AspectRatio();
	const XMMATRIX viewProjExpanded = view * XMMatrixPerspectiveFovLH(
		kCameraFovYRad * kShadowCullFovScale,
		aspect,
		kCameraNearZ,
		kCameraFarZ);
	const Aabb& shadowBounds =
		mShadowSceneBounds.IsValid() ? mShadowSceneBounds : mSponzaWorldBounds;
	mShadowDrawSponza = mSponzaWorldBounds.IsValid() &&
		mFrustum.IntersectsAabb(shadowBounds, viewProjExpanded);
}

UINT ObjTexturesDemoApp::CountShadowDrawCalls() const
{
	const UINT sceneDraws =
		(mShadowDrawSponza && mSceneGeo && !mSceneSubmeshes.empty())
			? static_cast<UINT>(mSceneSubmeshes.size())
			: 0u;
	const UINT propDraws =
		(mPropGeo && !mPropSubmeshes.empty()) ? static_cast<UINT>(mPropSubmeshes.size()) : 0u;
	return (sceneDraws + propDraws) * kShadowCascadeCount;
}

void ObjTexturesDemoApp::UpdateCameraAttachedSpotLight()
{
	if (mSceneLights.size() < 2u || mSceneLights[1].Type != kLightTypeSpot)
		return;

	XMVECTOR forward = CameraForwardNormalized();

	GpuLight& spot = mSceneLights[1];
	const XMVECTOR eye = XMLoadFloat3(&mCameraPos);
	const XMVECTOR spotPos = XMVectorAdd(eye, XMVectorScale(forward, 2.0f));
	XMStoreFloat3(&spot.Position, spotPos);
	XMStoreFloat3(&spot.Direction, forward);
}

void ObjTexturesDemoApp::OnMouseDown(WPARAM /*btnState*/, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;
	mSkipNextMouseLook = true;

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
		if (mSkipNextMouseLook)
		{
			mSkipNextMouseLook = false;
		}
		else
		{
			const float dx = mMouseSensitivity * static_cast<float>(x - mLastMousePos.x);
			const float dy = mMouseSensitivity * static_cast<float>(y - mLastMousePos.y);

			mYaw += dx;
			mPitch -= dy;
			const float pitchLimit = XM_PIDIV2 - 0.05f;
			mPitch = MathUtils::Clamp(mPitch, -pitchLimit, pitchLimit);
		}
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
	static constexpr UINT kMaxInstances = kMaxObjectDrawCalls;
	mObjectCbElementSize = Dx12Utils::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	mObjectCB = std::make_unique<GpuUploadBuffer<ObjectConstants>>(md3dDevice.Get(), kMaxInstances, true);
}

// Root signature геометрического прохода Lab 3/8: b0 = ObjectCB, t0..t3 = diff/normal/disp/ARM
void ObjTexturesDemoApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE srvTable{};
	srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);

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
		{"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
	static_assert(
		sizeof(Vertex) >= 32u,
		"Shadow pass reads POSITION+NORMAL+TEXCOORD (32 bytes); keep Vertex compatible.");
}

namespace
{
XMFLOAT3 TangentFromNormalCpu(XMFLOAT3 n)
{
	const XMFLOAT3 up = (std::fabs(n.y) > 0.999f) ? XMFLOAT3(1.f, 0.f, 0.f) : XMFLOAT3(0.f, 1.f, 0.f);
	XMVECTOR t = XMVector3Cross(XMLoadFloat3(&up), XMLoadFloat3(&n));
	if (XMVectorGetX(XMVector3LengthSq(t)) < 1e-8f)
		return {1.f, 0.f, 0.f};
	XMFLOAT3 out{};
	XMStoreFloat3(&out, XMVector3Normalize(t));
	return out;
}

void ComputeMeshTangents(std::vector<Vertex>& verts, const std::vector<uint32_t>& indices)
{
	for (size_t tri = 0; tri + 2 < indices.size(); tri += 3)
	{
		Vertex& v0 = verts[indices[tri]];
		Vertex& v1 = verts[indices[tri + 1]];
		Vertex& v2 = verts[indices[tri + 2]];

		const XMVECTOR p0 = XMLoadFloat3(&v0.Pos);
		const XMVECTOR p1 = XMLoadFloat3(&v1.Pos);
		const XMVECTOR p2 = XMLoadFloat3(&v2.Pos);

		const float du1 = v1.TexC.x - v0.TexC.x;
		const float dv1 = v1.TexC.y - v0.TexC.y;
		const float du2 = v2.TexC.x - v0.TexC.x;
		const float dv2 = v2.TexC.y - v0.TexC.y;
		const float denom = du1 * dv2 - du2 * dv1;

		XMFLOAT3 fallback = TangentFromNormalCpu(v0.Normal);
		if (std::fabs(denom) < 1e-8f)
		{
			v0.Tangent = {fallback.x, fallback.y, fallback.z, 1.f};
			const XMFLOAT3 fb1 = TangentFromNormalCpu(v1.Normal);
			v1.Tangent = {fb1.x, fb1.y, fb1.z, 1.f};
			const XMFLOAT3 fb2 = TangentFromNormalCpu(v2.Normal);
			v2.Tangent = {fb2.x, fb2.y, fb2.z, 1.f};
			continue;
		}

		const float r = 1.f / denom;
		const XMVECTOR edge1 = XMVectorSubtract(p1, p0);
		const XMVECTOR edge2 = XMVectorSubtract(p2, p0);
		const XMVECTOR tan = XMVectorScale(
			XMVectorSubtract(XMVectorScale(edge1, dv2), XMVectorScale(edge2, dv1)),
			r);
		const XMVECTOR bitan = XMVectorScale(
			XMVectorSubtract(XMVectorScale(edge2, du1), XMVectorScale(edge1, du2)),
			r);

		for (int k = 0; k < 3; ++k)
		{
			Vertex& v = verts[indices[tri + static_cast<size_t>(k)]];
			XMVECTOR n = XMLoadFloat3(&v.Normal);
			XMVECTOR tOrtho = XMVectorSubtract(tan, XMVectorMultiply(n, XMVector3Dot(tan, n)));
			float handedness = 1.f;
			if (XMVectorGetX(XMVector3LengthSq(tOrtho)) < 1e-8f)
			{
				XMFLOAT3 fb = TangentFromNormalCpu(v.Normal);
				tOrtho = XMLoadFloat3(&fb);
			}
			else
			{
				tOrtho = XMVector3Normalize(tOrtho);
				if (XMVectorGetX(XMVector3Dot(XMVector3Cross(n, tOrtho), bitan)) < 0.f)
					handedness = -1.f;
			}

			XMFLOAT4 out{};
			XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&out), tOrtho);
			out.w = handedness;
			v.Tangent = out;
		}
	}
}
} // namespace

void ObjTexturesDemoApp::CreateSrvForTexture(int heapIndex, ID3D12Resource* tex)
{
	D3D12_CPU_DESCRIPTOR_HANDLE h = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(heapIndex) * mCbvSrvUavDescriptorSize;
	Dx12Utils::CreateTextureSrv(md3dDevice.Get(), tex, h, false);
}

std::unique_ptr<MeshGeometry> ObjTexturesDemoApp::BuildModelGeometry(const ObjMeshData& data, const char* name)
{
	const size_t n = data.Positions.size();
	std::vector<Vertex> verts(n);
	for (size_t i = 0; i < n; ++i)
	{
		verts[i].Pos = data.Positions[i];
		verts[i].Normal = data.Normals[i];
		verts[i].TexC = data.Texcoords[i];
	}
	ComputeMeshTangents(verts, data.Indices32);

	const UINT vbByteSize = static_cast<UINT>(sizeof(Vertex) * verts.size());
	const UINT ibByteSize = static_cast<UINT>(sizeof(uint32_t) * data.Indices32.size());

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = name ? name : "ObjModel";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), verts.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), data.Indices32.data(), ibByteSize);

	geo->VertexBufferGPU = Dx12Utils::CreateDefaultBuffer(
		md3dDevice.Get(),
		mCommandList.Get(),
		verts.data(),
		vbByteSize,
		geo->VertexBufferUploader);

	geo->IndexBufferGPU = Dx12Utils::CreateDefaultBuffer(
		md3dDevice.Get(),
		mCommandList.Get(),
		data.Indices32.data(),
		ibByteSize,
		geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->IndexCount = static_cast<UINT>(data.Indices32.size());
	geo->StartIndexLocation = 0;
	geo->BaseVertexLocation = 0;

	return geo;
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

	CD3DX12_RASTERIZER_DESC rockRs(D3D12_DEFAULT);
	rockRs.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.RasterizerState = rockRs;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mDeferredGeoRockPSO)));

	CD3DX12_RASTERIZER_DESC wireRs(D3D12_DEFAULT);
	wireRs.FillMode = D3D12_FILL_MODE_WIREFRAME;
	wireRs.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.RasterizerState = wireRs;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mDeferredGeoWirePSO)));

	if (!mRenderer.TessSolidVsByteCode())
		throw DxException(
			E_FAIL,
			L"Solid geometry vertex shader is not initialized.",
			AnsiToWString(__FILE__),
			__LINE__);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC solidDesc = psoDesc;
	solidDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	solidDesc.RasterizerState.FrontCounterClockwise = FALSE;
	solidDesc.VS = {
		reinterpret_cast<BYTE*>(mRenderer.TessSolidVsByteCode()->GetBufferPointer()),
		mRenderer.TessSolidVsByteCode()->GetBufferSize()};
	solidDesc.HS = {};
	solidDesc.DS = {};
	solidDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&solidDesc, IID_PPV_ARGS(&mDeferredGeoSolidPSO)));

	CD3DX12_RASTERIZER_DESC noCullRs(D3D12_DEFAULT);
	noCullRs.CullMode = D3D12_CULL_MODE_NONE;
	solidDesc.RasterizerState = noCullRs;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&solidDesc, IID_PPV_ARGS(&mDeferredGeoSolidNoCullPSO)));
}

void ObjTexturesDemoApp::UpdateWindowCaption()
{
	wchar_t cap[160];
	const wchar_t* vig = mRenderer.PostVignetteEnabled() ? L"on" : L"off";
	const wchar_t* chr = mRenderer.PostChromaticEnabled() ? L"on" : L"off";
	const wchar_t* vaseAnim = mVaseVertexAnimEnabled ? L"on" : L"off";
	const wchar_t* lightRain = mLightRainEnabled ? L"on" : L"off";

	if (mShadowDrawOverflow || mGeometryDrawOverflow)
	{
		const wchar_t* cbTag = mGeometryDrawOverflow ? L"geo CB!" : L"shd CB!";
		swprintf_s(
			cap,
			160,
			L"Lab 8 | %.0f FPS | rain %s vase %s | %s | vig %s chr %s",
			mDisplayFps,
			lightRain,
			vaseAnim,
			cbTag,
			vig,
			chr);
	}
	else
	{
		swprintf_s(
			cap,
			160,
			L"Lab 8 | %.0f FPS | rain %u/%u vase %s | perl %.0f | shd %u/%u | vig %s chr %s",
			mDisplayFps,
			mLightRain.LandedDropCount(),
			mLightRain.ActiveDropCount(),
			vaseAnim,
			mPerlinSeed,
			mShadowDrawSlotsUsed,
			mShadowDrawCallsNeeded,
			vig,
			chr);
	}
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

	GpuLight pt{};
	pt.Type = kLightTypePoint;
	pt.Position = XMFLOAT3(0.f, 60.f, 0.f);
	pt.Range = 400.f;
	pt.Color = XMFLOAT3(1.f, 0.95f, 0.88f);
	pt.Intensity = kLocalLightIntensity * 0.35f;
	pt.Direction = XMFLOAT3(0.f, -1.f, 0.f);
	pt.SpotInnerCos = 0.f;
	pt.SpotOuterCos = 0.f;
	pt.Padding = XMFLOAT2(0.f, 0.f);
	mSceneLights.push_back(pt);

	GpuLight sp{};
	sp.Type = kLightTypeSpot;
	sp.Position = mCameraPos;
	sp.Direction = XMFLOAT3(0.f, 0.f, 1.f);
	sp.Range = kCameraSpotRange;
	sp.Color = XMFLOAT3(0.85f, 0.92f, 1.f);
	sp.Intensity = kLocalLightIntensity * kCameraSpotIntensityMul;
	sp.SpotInnerCos = cosf(XMConvertToRadians(kCameraSpotInnerAngleDeg));
	sp.SpotOuterCos = cosf(XMConvertToRadians(kCameraSpotOuterAngleDeg));
	sp.Padding = XMFLOAT2(0.f, 0.f);
	mSceneLights.push_back(sp);

	UpdateCameraAttachedSpotLight();
	UploadSceneLights();
}

LRESULT ObjTexturesDemoApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_KEYDOWN:
		if ((lParam & 0x40000000) == 0)
		{
			if (wParam == VK_F2)
			{
				mRenderer.SetPostVignetteEnabled(!mRenderer.PostVignetteEnabled());
				UpdateWindowCaption();
				return 0;
			}
			if (wParam == VK_F4)
			{
				mRenderer.SetPostChromaticEnabled(!mRenderer.PostChromaticEnabled());
				UpdateWindowCaption();
				return 0;
			}
			if (wParam == VK_F3)
			{
				mVaseVertexAnimEnabled = !mVaseVertexAnimEnabled;
				UpdateWindowCaption();
				return 0;
			}
			if (wParam == VK_F5)
			{
				mLightRainEnabled = !mLightRainEnabled;
				mLightRain.SetEnabled(mLightRainEnabled);
				UploadSceneLights();
				UpdateWindowCaption();
				return 0;
			}
			if (wParam == VK_F6)
			{
				mPerlinSeed += 1.0f;
				UpdateWindowCaption();
				return 0;
			}
		}
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
		case VK_SHIFT:
		case VK_LSHIFT:
		case VK_RSHIFT:
			mKeyBoost = true;
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
		case VK_SHIFT:
		case VK_LSHIFT:
		case VK_RSHIFT:
			mKeyBoost = false;
			return 0;
		}
		break;
	}

	return D3d12AppBase::MsgProc(hwnd, msg, wParam, lParam);
}