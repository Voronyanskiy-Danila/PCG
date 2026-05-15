
#include "ObjTexturesDemoApp.h"

#include "../math/MathUtils.h"
#include "../rendering/d3d12/D3d12_GpuUploadBuffer.h"
#include "../importers/Importer_Image_DirectXTex.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
				   PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

    try
    {
        CubeApp theApp(hInstance);
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

CubeApp::CubeApp(HINSTANCE hInstance)
: AppBase(hInstance)
{
	mMainWndCaption = L"Sponza — D3D12";
}

CubeApp::~CubeApp()
{
}

bool CubeApp::Initialize()
{
	if (!AppBase::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	LoadModelAndTextures();

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildPSO();

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = {mCommandList.Get()};
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	FlushCommandQueue();

	mTextureUploads.clear();

	return true;
}

void CubeApp::OnResize()
{
	AppBase::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathUtils::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void CubeApp::Update(const FrameTimer& gt)
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

void CubeApp::Draw(const FrameTimer&)
{
    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(mDirectCmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage: Present -> RenderTarget.
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        mCommandList->ResourceBarrier(1, &barrier);
    }

    // Clear the back buffer and depth buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::White, 0, nullptr);
    mCommandList->ClearDepthStencilView(
        DepthStencilView(),
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    // FIX: cannot take address of a temporary handle.
    const auto rtv = CurrentBackBufferView();
    const auto dsv = DepthStencilView();
    mCommandList->OMSetRenderTargets(1, &rtv, TRUE, &dsv);

    ID3D12DescriptorHeap* descriptorHeaps[] = {mSrvHeap.Get()};
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

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

    // Indicate a state transition on the resource usage: RenderTarget -> Present.
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
        mCommandList->ResourceBarrier(1, &barrier);
    }

    // Done recording commands.
    ThrowIfFailed(mCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers.
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Wait until frame commands are complete.
    FlushCommandQueue();
}

void CubeApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void CubeApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void CubeApp::OnMouseMove(WPARAM btnState, int x, int y)
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

void CubeApp::BuildDescriptorHeaps(UINT srvCount)
{
	mSrvDescriptorCount = srvCount;
	D3D12_DESCRIPTOR_HEAP_DESC dh{};
	dh.NumDescriptors = srvCount;
	dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	dh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	dh.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&mSrvHeap)));
}

void CubeApp::BuildConstantBuffers()
{
	mObjectCB = std::make_unique<GpuUploadBuffer<ObjectConstants>>(md3dDevice.Get(), 1, true);
}

void CubeApp::BuildRootSignature()
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

void CubeApp::BuildShadersAndInputLayout()
{
	mvsByteCode = Dx12Utils::CompileShader(
		L"content/shaders/phong.hlsl",
		nullptr,
		"VS",
		"vs_5_0");

	mpsByteCode = Dx12Utils::CompileShader(
		L"content/shaders/phong.hlsl",
		nullptr,
		"PS",
		"ps_5_0");

	mInputLayout = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
}

void CubeApp::CreateSrvForTexture(int heapIndex, ID3D12Resource* tex)
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

void CubeApp::BuildModelGeometry(const ObjMeshData& data)
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

	SubmeshGeometry submesh{};
	submesh.IndexCount = static_cast<UINT>(data.Indices32.size());
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;
	mModelGeo->DrawArgs["model"] = submesh;
}

void CubeApp::FitWorldAndCameraToMesh(const ObjMeshData& data)
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

void CubeApp::LoadModelAndTextures()
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

	const UINT srvCount = 1u + static_cast<UINT>(loadOrder.size());
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

void CubeApp::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.InputLayout = {mInputLayout.data(), (UINT)mInputLayout.size()};
	psoDesc.pRootSignature = mRootSignature.Get();
	psoDesc.VS = {
		reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
		mvsByteCode->GetBufferSize()};
	psoDesc.PS = {
		reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
		mpsByteCode->GetBufferSize()};
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	// OBJ Sponza: в экране передняя грань по часовой; CCW-as-front (TRUE) даёт «изнанку».
	psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = mBackBufferFormat;
	psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	psoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}

LRESULT CubeApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
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

	return AppBase::MsgProc(hwnd, msg, wParam, lParam);
}