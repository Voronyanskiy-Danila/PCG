
#include "ObjTexturesDemoApp.h"

#include <DirectXColors.h>
#include <filesystem>

#include "../math/MathUtils.h"
#include "../rendering/GBuffer.h"
#include "../rendering/d3d12/D3d12_GpuUploadBuffer.h"
#include "../importers/Importer_Image_DirectXTex.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>

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
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
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

void ObjTexturesDemoApp::OnResize()
{
	D3d12AppBase::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathUtils::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);

	mRenderer.ResizeGBuffer(md3dDevice.Get(), static_cast<UINT>(mClientWidth), static_cast<UINT>(mClientHeight));
	if (mSrvHeap)
		RefreshDeferredSrvs();
}

void ObjTexturesDemoApp::Update(const FrameTimer& gt)
{
	const float dt = gt.DeltaTime();
	const float speed = mCameraSpeed * dt;

	const float cp = cosf(mPitch);
	const float sp = sinf(mPitch);
	const float cy = cosf(mYaw);
	const float sy = sinf(mYaw);

	XMVECTOR forward = XMVectorSet(sy * cp, sp, cy * cp, 0.0f);
	forward = XMVector3Normalize(forward);

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

	obj.UvAnimParams = XMFLOAT4(gt.TotalTime(), 0.0f, 0.0f, 0.0f);
	obj.UvScale = mUvScale;
	obj.UvScroll = mUvScroll;

	mSharedConstants = obj;
}

void ObjTexturesDemoApp::Draw(const FrameTimer&)
{
    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

	ID3D12DescriptorHeap* descriptorHeaps[] = {mSrvHeap.Get()};
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mRenderer.TransitionGbufferToRenderTarget(mCommandList.Get());

	GBuffer* gb = mRenderer.GetGBuffer();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsvGbuffer = gb->DsvCpu();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvMrt[GBuffer::kRtCount]{};
	for (UINT i = 0; i < GBuffer::kRtCount; ++i)
		rtvMrt[i] = gb->RtvCpu(i);
	mCommandList->OMSetRenderTargets(GBuffer::kRtCount, rtvMrt, false, &dsvGbuffer);

	const float clear0[4] = {0.f, 0.f, 0.f, 0.f};
	for (UINT i = 0; i < GBuffer::kRtCount; ++i)
		mCommandList->ClearRenderTargetView(rtvMrt[i], clear0, 0, nullptr);
	mCommandList->ClearDepthStencilView(
		dsvGbuffer,
		D3D12_CLEAR_FLAG_DEPTH,
		1.0f, 0, 0, nullptr);

	mCommandList->SetPipelineState(mDeferredGeoPSO.Get());
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	const auto vbv = mModelGeo->VertexBufferView();
	mCommandList->IASetVertexBuffers(0, 1, &vbv);
	const auto ibv = mModelGeo->IndexBufferView();
	mCommandList->IASetIndexBuffer(&ibv);
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	const UINT srvIncr = mCbvSrvUavDescriptorSize;
	const CD3DX12_GPU_DESCRIPTOR_HANDLE srvBase(mSrvHeap->GetGPUDescriptorHandleForHeapStart());

	for (const DrawSubmesh& sm : mDrawSubmeshes)
	{
		ObjectConstants per = mSharedConstants;
		per.MatKa = sm.Ka;
		per.MatKd = sm.Kd;
		per.MatKs = sm.Ks;
		per.MatNs = sm.Ns;
		per.HasDiffuseTexture = sm.HasDiffuseTexture ? 1.0f : 0.0f;

		mObjectCB->CopyData(0, per);
		mCommandList->SetGraphicsRootConstantBufferView(0, mObjectCB->Resource()->GetGPUVirtualAddress());

		CD3DX12_GPU_DESCRIPTOR_HANDLE texH(srvBase);
		texH.Offset(sm.DiffuseSrvIndex, srvIncr);
		mCommandList->SetGraphicsRootDescriptorTable(1, texH);

		mCommandList->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndexLocation, 0, 0);
	}

	mRenderer.TransitionGbufferToPixelShader(mCommandList.Get());

	{
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        mCommandList->ResourceBarrier(1, &barrier);
    }

	const auto bbRtv = CurrentBackBufferView();
	mCommandList->OMSetRenderTargets(1, &bbRtv, false, nullptr);
	mCommandList->ClearRenderTargetView(bbRtv, Colors::Black, 0, nullptr);

	const XMMATRIX viewMat = XMLoadFloat4x4(&mView);
	const XMMATRIX projMat = XMLoadFloat4x4(&mProj);
	mRenderer.UpdateLightingFrameConstants(md3dDevice.Get(), viewMat, projMat, mCameraPos);

	mRenderer.SetLightingPipeline(mCommandList.Get());
	mCommandList->SetGraphicsRootConstantBufferView(
		0,
		mRenderer.LightingCb().Resource()->GetGPUVirtualAddress());

	const CD3DX12_GPU_DESCRIPTOR_HANDLE lightSrv = mRenderer.LightingSrvGpuStart(
		mSrvHeap.Get(),
		mDeferredSrvHeapBase,
		srvIncr);
	mCommandList->SetGraphicsRootDescriptorTable(1, lightSrv);

	mCommandList->IASetVertexBuffers(0, 0, nullptr);

	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mCommandList->DrawInstanced(3, 1, 0, 0);

    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
        mCommandList->ResourceBarrier(1, &barrier);
    }

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    FlushCommandQueue();
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
	if (data.Positions.empty())
		return;

	XMFLOAT3 mn = data.Positions[0];
	XMFLOAT3 mx = data.Positions[0];
	for (const XMFLOAT3& p : data.Positions)
	{
		mn.x = (std::min)(mn.x, p.x);
		mn.y = (std::min)(mn.y, p.y);
		mn.z = (std::min)(mn.z, p.z);
		mx.x = (std::max)(mx.x, p.x);
		mx.y = (std::max)(mx.y, p.y);
		mx.z = (std::max)(mx.z, p.z);
	}

	const XMVECTOR mnV = XMLoadFloat3(&mn);
	const XMVECTOR mxV = XMLoadFloat3(&mx);
	const XMVECTOR center = XMVectorScale(XMVectorAdd(mnV, mxV), 0.5f);
	XMFLOAT3 centerF;
	XMStoreFloat3(&centerF, center);

	const XMVECTOR ext = XMVectorSubtract(mxV, mnV);
	const float ex = (std::max)({ XMVectorGetX(ext), XMVectorGetY(ext), XMVectorGetZ(ext) });
	constexpr float kTargetExtents = 30.0f;
	const float s = (ex > 1e-6f) ? (kTargetExtents / ex) : 1.0f;

	const XMMATRIX world =
		XMMatrixTranslation(-centerF.x, -centerF.y, -centerF.z) * XMMatrixScaling(s, s, s);
	XMStoreFloat4x4(&mWorld, world);

	const float diagonal = XMVectorGetX(XMVector3Length(ext));
	const float sceneSize = diagonal * s;
	const float dist = (std::min)(200.0f, (std::max)(14.0f, sceneSize * 0.62f));

	// Стартовая позиция как у прежней орбиты; yaw/pitch выставляем так, чтобы смотреть на центр сцены.
	const float theta = 1.5f * XM_PI;
	const float phi = XM_PIDIV4;
	const XMVECTOR pos = MathUtils::SphericalToCartesian(dist, theta, phi);
	XMStoreFloat3(&mCameraPos, pos);
	const XMVECTOR toCenter = XMVector3Normalize(XMVectorNegate(pos));
	mYaw = atan2f(XMVectorGetX(toCenter), XMVectorGetZ(toCenter));
	mPitch = asinf(MathUtils::Clamp(XMVectorGetY(toCenter), -0.99f, 0.99f));
}

void ObjTexturesDemoApp::LoadModelAndTextures()
{
	std::wstring err;
	ObjMeshData data;
	const wchar_t* kObjPath = L"content/models/sponza/sponza.obj";

	if (!LoadWavefrontObj(kObjPath, data, err))
	{
		std::wstring msg = err;
		msg += L"\n\nExpected: content/models/sponza/sponza.obj, sponza.mtl, textures/*.tga";
		throw DxException(E_FAIL, msg, AnsiToWString(__FILE__), __LINE__);
	}

	std::unordered_set<std::string> usedMat;
	for (const auto& sm : data.Submeshes)
		usedMat.insert(sm.MaterialName);

	std::unordered_map<std::wstring, int> texPathToSrv;
	std::vector<std::wstring> loadOrder;

	for (const auto& matName : usedMat)
	{
		auto itMat = data.Materials.find(matName);
		if (itMat == data.Materials.end())
			continue;
		const std::wstring& p = itMat->second.DiffuseTexturePath;
		if (!p.empty() && texPathToSrv.find(p) == texPathToSrv.end())
		{
			texPathToSrv[p] = static_cast<int>(1 + loadOrder.size());
			loadOrder.push_back(p);
		}
	}

	mDeferredSrvHeapBase = 1u + static_cast<UINT>(loadOrder.size());
	const UINT srvCount = mDeferredSrvHeapBase + GBuffer::kRtCount + 1u;
	BuildDescriptorHeaps(srvCount);
	BuildConstantBuffers();

	mTextureGPU.assign(srvCount, {});
	mTextureUploads.assign(srvCount, {});

	HRESULT hr = LoadTextureImageFromFile12(
		md3dDevice.Get(),
		mCommandList.Get(),
		L"content/models/white.dds",
		mTextureGPU[0],
		mTextureUploads[0]);
	ThrowIfFailed(hr);
	CreateSrvForTexture(0, mTextureGPU[0].Get());

	for (size_t i = 0; i < loadOrder.size(); ++i)
	{
		const UINT slot = static_cast<UINT>(1u + i);
		hr = LoadTextureImageFromFile12(
			md3dDevice.Get(),
			mCommandList.Get(),
			loadOrder[i].c_str(),
			mTextureGPU[slot],
			mTextureUploads[slot]);
		if (FAILED(hr))
		{
			hr = LoadTextureImageFromFile12(
				md3dDevice.Get(),
				mCommandList.Get(),
				L"content/models/white.dds",
				mTextureGPU[slot],
				mTextureUploads[slot]);
		}
		ThrowIfFailed(hr);
		CreateSrvForTexture(static_cast<int>(slot), mTextureGPU[slot].Get());
	}

	BuildModelGeometry(data);

	mDrawSubmeshes.clear();
	for (const auto& sm : data.Submeshes)
	{
		DrawSubmesh d{};
		d.StartIndexLocation = sm.StartIndexLocation;
		d.IndexCount = sm.IndexCount;
		auto itMat = data.Materials.find(sm.MaterialName);
		if (itMat == data.Materials.end())
		{
			d.DiffuseSrvIndex = 0;
			d.HasDiffuseTexture = false;
			mDrawSubmeshes.push_back(d);
			continue;
		}
		const MtlMaterial& m = itMat->second;
		d.Ka = m.Ka;
		d.Kd = m.Kd;
		d.Ks = m.Ks;
		d.Ns = m.Ns;
		const bool hasTex = !m.DiffuseTexturePath.empty();
		d.HasDiffuseTexture = hasTex;
		if (!hasTex)
			d.DiffuseSrvIndex = 0;
		else
		{
			auto tp = texPathToSrv.find(m.DiffuseTexturePath);
			d.DiffuseSrvIndex = (tp != texPathToSrv.end()) ? tp->second : 0;
		}
		mDrawSubmeshes.push_back(d);
	}

	FitWorldAndCameraToMesh(data);
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
	mSceneLights.clear();

	GpuLight sun{};
	sun.Type = kLightTypeDirectional;
	sun.Direction = XMFLOAT3(0.577f, -0.577f, 0.577f);
	{
		const XMVECTOR d = XMVector3Normalize(XMLoadFloat3(&sun.Direction));
		XMStoreFloat3(&sun.Direction, d);
	}
	sun.Color = XMFLOAT3(1.f, 1.f, 1.f);
	sun.Intensity = 1.0f;
	mSceneLights.push_back(sun);

	GpuLight pt{};
	pt.Type = kLightTypePoint;
	pt.Position = XMFLOAT3(8.f, 14.f, 0.f);
	pt.Range = 35.f;
	pt.Color = XMFLOAT3(0.6f, 0.75f, 1.f);
	pt.Intensity = 2.5f;
	mSceneLights.push_back(pt);

	GpuLight sp{};
	sp.Type = kLightTypeSpot;
	sp.Position = XMFLOAT3(-10.f, 22.f, 8.f);
	sp.Direction = XMFLOAT3(0.3f, -0.85f, -0.2f);
	{
		const XMVECTOR sd = XMVector3Normalize(XMLoadFloat3(&sp.Direction));
		XMStoreFloat3(&sp.Direction, sd);
	}
	sp.Range = 45.f;
	sp.Color = XMFLOAT3(1.f, 0.92f, 0.75f);
	sp.Intensity = 3.2f;
	sp.SpotInnerCos = cosf(XMConvertToRadians(18.f));
	sp.SpotOuterCos = cosf(XMConvertToRadians(32.f));
	mSceneLights.push_back(sp);

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