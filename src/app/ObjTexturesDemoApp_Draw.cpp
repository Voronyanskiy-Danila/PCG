// =============================================================================
// ObjTexturesDemoApp_Draw.cpp — кадр: G-buffer → lighting → particles (Lab 5)
// =============================================================================

#include "ObjTexturesDemoApp.h"

#include "../rendering/GBuffer.h"
#include "../rendering/ShadowStructures.h"

#include <DirectXColors.h>

// Точка входа рендера: три последовательных этапа на command list
void ObjTexturesDemoApp::Draw(const FrameTimer& gt)
{
	StartDeferredFrameRecording();
	RunShadowPass();
	BindAndClearGBuffer();
	mRenderer.UpdateParticles(mCommandList.Get(), gt.DeltaTime());
	ID3D12DescriptorHeap* heaps[] = {mSrvHeap.Get()};
	mCommandList->SetDescriptorHeaps(static_cast<UINT>(_countof(heaps)), heaps);
	RunDeferredGeometryPass();
	RunDeferredLightingPass();      // Lab 2: чтение G-buffer → экран (backbuffer остается RT)
	const XMMATRIX view = XMLoadFloat4x4(&mView);
	const XMMATRIX proj = XMLoadFloat4x4(&mProj);
	const XMMATRIX invView = XMMatrixInverse(nullptr, view);
	const XMVECTOR cameraRight = XMVector3Normalize(invView.r[0]);
	const XMVECTOR cameraUp = XMVector3Normalize(invView.r[1]);
	mRenderer.DrawParticles(
		mCommandList.Get(),
		CurrentBackBufferView(),
		mRenderer.GetGBuffer()->DsvCpu(),
		mScreenViewport,
		mScissorRect,
		view,
		proj,
		cameraRight,
		cameraUp);
	UpdateWindowCaption();

	SubmitCommandListPresentAndFlush(); // Execute, Present, sync
}

void ObjTexturesDemoApp::StartDeferredFrameRecording()
{
	ThrowIfFailed(mDirectCmdListAlloc->Reset());           // освободить allocator для новых команд
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr)); // начать запись list

	ID3D12GraphicsCommandList* const cmd = mCommandList.Get();
	cmd->RSSetViewports(1, &mScreenViewport);            // размер окна для растеризации
	cmd->RSSetScissorRects(1, &mScissorRect);              // область отсечения = viewport

	ID3D12DescriptorHeap* heaps[] = {mSrvHeap.Get()};
	cmd->SetDescriptorHeaps(static_cast<UINT>(_countof(heaps)), heaps);
}

void ObjTexturesDemoApp::BindAndClearGBuffer()
{
	ID3D12GraphicsCommandList* const cmd = mCommandList.Get();
	cmd->RSSetViewports(1, &mScreenViewport);
	cmd->RSSetScissorRects(1, &mScissorRect);

	mRenderer.TransitionGbufferToRenderTarget(cmd);

	GBuffer* const gb = mRenderer.GetGBuffer();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsvGb = gb->DsvCpu();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvMrt[GBuffer::kRtCount]{};
	for (UINT i = 0u; i < GBuffer::kRtCount; ++i)
		rtvMrt[i] = gb->RtvCpu(i);

	cmd->OMSetRenderTargets(GBuffer::kRtCount, rtvMrt, false, &dsvGb);

	static const float kClearRt[4] = {0.f, 0.f, 0.f, 0.f};
	for (UINT i = 0u; i < GBuffer::kRtCount; ++i)
		cmd->ClearRenderTargetView(rtvMrt[i], kClearRt, 0, nullptr);

	cmd->ClearDepthStencilView(dsvGb, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0u, 0, nullptr);
}

void ObjTexturesDemoApp::RunShadowPass()
{
	ID3D12GraphicsCommandList* const cmd = mCommandList.Get();

	const XMMATRIX view = XMLoadFloat4x4(&mView);
	const XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMFLOAT3 camForward{};
	XMStoreFloat3(&camForward, CameraForwardNormalized());

	mRenderer.UpdateShadowCascades(
		view,
		proj,
		mDirLightW,
		mCameraPos,
		camForward,
		mSceneWorldBounds,
		kCameraNearZ,
		kCameraFarZ,
		kCameraFovYRad,
		AspectRatio());

	const UINT shadowDrawsNeeded = CountShadowDrawCalls();
	mShadowDrawOverflow = shadowDrawsNeeded > kMaxShadowDrawCalls;
	mShadowDrawSlotsUsed = 0u;

	if (mShadowDrawOverflow)
	{
		for (UINT cascade = 0u; cascade < kShadowCascadeCount; ++cascade)
			mRenderer.BeginShadowPass(cmd, cascade);
		mRenderer.EndShadowPass(cmd);
		cmd->RSSetViewports(1, &mScreenViewport);
		cmd->RSSetScissorRects(1, &mScissorRect);
		return;
	}

	const ShadowLightingConstants& shadowLit = mRenderer.Shadows().GetLightingConstants();
	const UINT shadowCbStride = mRenderer.ShadowDrawCbElementSize();
	const UINT64 shadowCbGpuBase = mRenderer.ShadowDrawCb().Resource()->GetGPUVirtualAddress();
	UINT shadowCbSlot = 0u;

	auto drawModelShadow = [&](const MeshGeometry& geo,
							   const std::vector<DrawSubmesh>& submeshes,
							   CXMMATRIX world,
							   UINT cascade) {
		const D3D12_VERTEX_BUFFER_VIEW vbv = geo.VertexBufferView();
		const D3D12_INDEX_BUFFER_VIEW ibv = geo.IndexBufferView();
		cmd->IASetVertexBuffers(0u, 1u, &vbv);
		cmd->IASetIndexBuffer(&ibv);
		cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		const XMMATRIX lightViewProj =
			XMMatrixTranspose(XMLoadFloat4x4(&shadowLit.LightViewProj[cascade]));

		for (const DrawSubmesh& sm : submeshes)
		{
			ShadowDrawConstants sd{};
			const XMMATRIX wvp = world * lightViewProj;
			XMStoreFloat4x4(&sd.WorldLightViewProj, XMMatrixTranspose(wvp));

			mRenderer.ShadowDrawCb().CopyData(static_cast<int>(shadowCbSlot), sd);
			cmd->SetGraphicsRootConstantBufferView(
				0u,
				shadowCbGpuBase + static_cast<UINT64>(shadowCbSlot) * static_cast<UINT64>(shadowCbStride));

			cmd->DrawIndexedInstanced(sm.IndexCount, 1u, sm.StartIndexLocation, 0, 0u);
			++shadowCbSlot;
		}
	};

	for (UINT cascade = 0u; cascade < kShadowCascadeCount; ++cascade)
	{
		mRenderer.BeginShadowPass(cmd, cascade);

		if (mShadowDrawSponza && mSceneGeo && !mSceneSubmeshes.empty())
			drawModelShadow(*mSceneGeo, mSceneSubmeshes, XMMatrixIdentity(), cascade);

		for (uint32_t instIdx : mShadowCastInstances)
		{
			const SceneInstance& inst = mInstances[instIdx];
			const XMMATRIX world = XMLoadFloat4x4(&inst.World);
			if (mRockGeo && !mRockSubmeshes.empty())
				drawModelShadow(*mRockGeo, mRockSubmeshes, world, cascade);
		}
	}

	mShadowDrawSlotsUsed = shadowCbSlot;

	mRenderer.EndShadowPass(cmd);

	cmd->RSSetViewports(1, &mScreenViewport);
	cmd->RSSetScissorRects(1, &mScissorRect);
}

void ObjTexturesDemoApp::RunDeferredGeometryPass()
{
	ID3D12GraphicsCommandList* const cmd = mCommandList.Get();

	const bool wireframe = (mTessDebugMode == 2);          // debug: каркас тесселированных треугольников
	cmd->SetPipelineState(
		wireframe ? mDeferredGeoWirePSO.Get() : mDeferredGeoPSO.Get()); // VS+HS+DS+PS tess
	cmd->SetGraphicsRootSignature(mRootSignature.Get()); // b0=CBV, table=t0,t1,t2

	const UINT srvIncr = mCbvSrvUavDescriptorSize;       // размер одного descriptor в heap
	const CD3DX12_GPU_DESCRIPTOR_HANDLE srvBase{mSrvHeap->GetGPUDescriptorHandleForHeapStart()};

	const XMMATRIX view = XMLoadFloat4x4(&mView);
	const XMMATRIX proj = XMLoadFloat4x4(&mProj);
	UINT cbSlot = 0u;

	auto drawModel = [&](const MeshGeometry& geo,
						 const std::vector<DrawSubmesh>& submeshes,
						 CXMMATRIX world) {
		const D3D12_VERTEX_BUFFER_VIEW vbv = geo.VertexBufferView();
		const D3D12_INDEX_BUFFER_VIEW ibv = geo.IndexBufferView();
		cmd->IASetVertexBuffers(0u, 1u, &vbv);
		cmd->IASetIndexBuffer(&ibv);
		cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

		const XMMATRIX wvp = world * view * proj;
		for (const DrawSubmesh& sm : submeshes)
		{
			ObjectConstants per = mSharedConstants;
			XMStoreFloat4x4(&per.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&per.WorldInvTranspose, XMMatrixTranspose(MathUtils::InverseTranspose(world)));
			XMStoreFloat4x4(&per.WorldViewProj, XMMatrixTranspose(wvp));
			per.MatKd = sm.Kd;
			per.MatKs = sm.Ks;
			per.MatNs = sm.Ns;
			per.HasDiffuseTexture = sm.HasDiffuseTexture ? 1.f : 0.f;
			per.HasNormalTexture = sm.HasNormalTexture ? 1.f : 0.f;

			mObjectCB->CopyData(static_cast<int>(cbSlot), per);
			const UINT64 cbGpu = mObjectCB->Resource()->GetGPUVirtualAddress() +
				static_cast<UINT64>(cbSlot) * static_cast<UINT64>(mObjectCbElementSize);
			cmd->SetGraphicsRootConstantBufferView(0u, cbGpu);
			++cbSlot;

			CD3DX12_GPU_DESCRIPTOR_HANDLE texH{srvBase};
			texH.Offset(sm.MaterialSrvBase, srvIncr);
			cmd->SetGraphicsRootDescriptorTable(1u, texH);

			cmd->DrawIndexedInstanced(sm.IndexCount, 1u, sm.StartIndexLocation, 0, 0u);
		}
	};

	if (mSceneGeo && !mSceneSubmeshes.empty())
		drawModel(*mSceneGeo, mSceneSubmeshes, XMMatrixIdentity());

	for (uint32_t instIdx : mVisibleInstances)
	{
		const SceneInstance& inst = mInstances[instIdx];
		const XMMATRIX world = XMLoadFloat4x4(&inst.World);
		if (mRockGeo && !mRockSubmeshes.empty())
			drawModel(*mRockGeo, mRockSubmeshes, world);
	}
}

void ObjTexturesDemoApp::RunDeferredLightingPass()
{
	ID3D12GraphicsCommandList* const cmd = mCommandList.Get();

	mRenderer.TransitionGbufferToPixelShader(cmd);         // RT G-buffer → SRV для PS_Light

	CD3DX12_RESOURCE_BARRIER toRt = CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	cmd->ResourceBarrier(1u, &toRt);                       // back buffer можно рисовать

	const D3D12_CPU_DESCRIPTOR_HANDLE bbRtv = CurrentBackBufferView();
	cmd->OMSetRenderTargets(1u, &bbRtv, false, nullptr);   // один RT = swap chain
	cmd->ClearRenderTargetView(bbRtv, Colors::Black, 0, nullptr);

	mRenderer.UpdateLightingFrameConstants(
		md3dDevice.Get(),
		mCameraPos,
		mDirLightW,
		XMFLOAT3(1.f, 0.98f, 0.92f),
		4.0f);

	mRenderer.SetLightingPipeline(cmd);
	cmd->SetGraphicsRootConstantBufferView(0u, mRenderer.LightingCb().Resource()->GetGPUVirtualAddress());
	cmd->SetGraphicsRootConstantBufferView(
		1u,
		mRenderer.ShadowLightingCb().Resource()->GetGPUVirtualAddress());

	const UINT srvIncr = mCbvSrvUavDescriptorSize;
	const CD3DX12_GPU_DESCRIPTOR_HANDLE lightSrv =
		mRenderer.LightingSrvGpuStart(mSrvHeap.Get(), mDeferredSrvHeapBase, srvIncr);
	cmd->SetGraphicsRootDescriptorTable(2u, lightSrv);

	cmd->IASetVertexBuffers(0u, 0u, nullptr);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->DrawInstanced(3u, 1u, 0u, 0u);                    // fullscreen triangle

}

void ObjTexturesDemoApp::SubmitCommandListPresentAndFlush()
{
	CD3DX12_RESOURCE_BARRIER toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);
	mCommandList->ResourceBarrier(1u, &toPresent);

	ThrowIfFailed(mCommandList->Close());                  // закончить запись команд

	ID3D12CommandList* lists[] = {mCommandList.Get()};
	mCommandQueue->ExecuteCommandLists(static_cast<UINT>(_countof(lists)), lists); // GPU старт

	ThrowIfFailed(mSwapChain->Present(0u, 0u));            // показать кадр
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
	FlushCommandQueue();                                   // дождаться GPU (учебный пример)
}
