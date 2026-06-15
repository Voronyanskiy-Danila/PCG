// =============================================================================
// ObjTexturesDemoApp_Draw.cpp — кадр: G-buffer → lighting → particles → post (Lab 7)
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
	RunDeferredLightingPass();
	const D3D12_CPU_DESCRIPTOR_HANDLE colorRtv =
		mRenderer.UsesSceneColorTarget()
			? mRenderer.SceneColorRtv()
			: CurrentBackBufferView();

	const XMMATRIX view = XMLoadFloat4x4(&mView);
	const XMMATRIX proj = XMLoadFloat4x4(&mProj);
	const XMMATRIX invView = XMMatrixInverse(nullptr, view);
	const XMVECTOR cameraRight = XMVector3Normalize(invView.r[0]);
	const XMVECTOR cameraUp = XMVector3Normalize(invView.r[1]);
	const bool usePostSceneRt = mRenderer.UsesSceneColorTarget();

	if (mLightRainEnabled)
	{
		mLightRainCircles.Draw(
			mCommandList.Get(),
			mLightRain,
			colorRtv,
			mRenderer.GetGBuffer()->DsvCpu(),
			!usePostSceneRt,
			mScreenViewport,
			mScissorRect,
			view,
			proj,
			cameraRight,
			cameraUp);
	}

	mRenderer.DrawParticles(
		mCommandList.Get(),
		colorRtv,
		mRenderer.GetGBuffer()->DsvCpu(),
		!usePostSceneRt,
		mScreenViewport,
		mScissorRect,
		view,
		proj,
		cameraRight,
		cameraUp);

	if (usePostSceneRt)
	{
		mCommandList->SetDescriptorHeaps(1u, heaps);
		mRenderer.RunPostProcess(
			mCommandList.Get(),
			CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT,
			mSrvHeap.Get(),
			mDeferredSrvHeapBase,
			mCbvSrvUavDescriptorSize,
			CurrentBackBufferView(),
			mScreenViewport,
			mScissorRect);
	}

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

	const Aabb& shadowBounds =
		mShadowSceneBounds.IsValid() ? mShadowSceneBounds : mSponzaWorldBounds;
	mRenderer.UpdateShadowCascades(
		view,
		proj,
		mDirLightW,
		mCameraPos,
		camForward,
		shadowBounds,
		kCameraNearZ,
		kCameraFarZ,
		kCameraFovYRad,
		AspectRatio());

	const UINT shadowDrawsNeeded = CountShadowDrawCalls();
	mShadowDrawCallsNeeded = shadowDrawsNeeded;
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
			if (mVaseVertexAnimEnabled && sm.VertexAnimEnabled)
			{
				sd.VertexAnimEnable = 1.f;
				sd.VertexAnimPivotX = sm.VertexAnimPivotX;
				sd.VertexAnimPivotY = sm.VertexAnimPivotY;
				sd.VertexAnimPivotZ = sm.VertexAnimPivotZ;
				sd.VertexAnimPhase = sm.VertexAnimPhase;
				sd.VertexAnimTime = mVertexAnimTime;
			}

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

		if (mPropGeo && !mPropSubmeshes.empty())
			drawModelShadow(*mPropGeo, mPropSubmeshes, XMLoadFloat4x4(&mPropWorld), cascade);
	}

	mShadowDrawSlotsUsed = shadowCbSlot;

	mRenderer.EndShadowPass(cmd);

	cmd->RSSetViewports(1, &mScreenViewport);
	cmd->RSSetScissorRects(1, &mScissorRect);
}

void ObjTexturesDemoApp::RunDeferredGeometryPass()
{
	ID3D12GraphicsCommandList* const cmd = mCommandList.Get();

	cmd->SetGraphicsRootSignature(mRootSignature.Get()); // b0=CBV, table=t0..t3

	const UINT srvIncr = mCbvSrvUavDescriptorSize;       // размер одного descriptor в heap
	const CD3DX12_GPU_DESCRIPTOR_HANDLE srvBase{mSrvHeap->GetGPUDescriptorHandleForHeapStart()};

	const XMMATRIX view = XMLoadFloat4x4(&mView);
	const XMMATRIX proj = XMLoadFloat4x4(&mProj);
	UINT cbSlot = 0u;
	mGeometryDrawOverflow = false;

	auto drawSubmeshes = [&](const MeshGeometry& geo,
							 const std::vector<DrawSubmesh>& submeshes,
							 CXMMATRIX world,
							 D3D12_PRIMITIVE_TOPOLOGY topology,
							 const ObjectConstants* baseConstants = nullptr) {
		if (mGeometryDrawOverflow)
			return;

		const D3D12_VERTEX_BUFFER_VIEW vbv = geo.VertexBufferView();
		const D3D12_INDEX_BUFFER_VIEW ibv = geo.IndexBufferView();
		cmd->IASetVertexBuffers(0u, 1u, &vbv);
		cmd->IASetIndexBuffer(&ibv);
		cmd->IASetPrimitiveTopology(topology);

		const XMMATRIX wvp = world * view * proj;
		for (const DrawSubmesh& sm : submeshes)
		{
			if (cbSlot >= kMaxObjectDrawCalls)
			{
				mGeometryDrawOverflow = true;
				return;
			}

			ObjectConstants per = baseConstants ? *baseConstants : mSharedConstants;
			XMStoreFloat4x4(&per.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&per.WorldInvTranspose, XMMatrixTranspose(MathUtils::InverseTranspose(world)));
			XMStoreFloat4x4(&per.WorldViewProj, XMMatrixTranspose(wvp));
			per.MatKd = sm.Kd;
			per.MatRoughness = sm.Roughness;
			per.MatMetallic = sm.Metallic;
			per.MatNsFallback = sm.NsFallback;
			per.HasDiffuseTexture = sm.HasDiffuseTexture ? 1.f : 0.f;
			per.HasNormalTexture = sm.HasNormalTexture ? 1.f : 0.f;
			per.HasRmTexture = sm.HasRmTexture ? 1.f : 0.f;
			per.NormalFlipY = sm.NormalFlipY ? 1.f : 0.f;
			per.UvScale = sm.UvScale;
			if (mVaseVertexAnimEnabled && sm.VertexAnimEnabled)
			{
				per.VertexAnimEnable = 1.f;
				per.VertexAnimPivotX = sm.VertexAnimPivotX;
				per.VertexAnimPivotY = sm.VertexAnimPivotY;
				per.VertexAnimPivotZ = sm.VertexAnimPivotZ;
				per.VertexAnimPhase = sm.VertexAnimPhase;
				per.VertexAnimTime = mVertexAnimTime;
			}
			else
			{
				per.VertexAnimEnable = 0.f;
			}

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

	cmd->SetPipelineState(mDeferredGeoSolidPSO.Get());
	if (mSceneGeo && !mSceneSubmeshes.empty())
	{
		drawSubmeshes(
			*mSceneGeo,
			mSceneSubmeshes,
			XMMatrixIdentity(),
			D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	if (mPropGeo && !mPropSubmeshes.empty())
	{
		drawSubmeshes(
			*mPropGeo,
			mPropSubmeshes,
			XMLoadFloat4x4(&mPropWorld),
			D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	// Камни: tess + displacement, double-sided (Lab 3)
	if (mRockGeo && !mRockSubmeshes.empty())
	{
		ID3D12PipelineState* rockPso = mDeferredGeoRockPSO.Get();
		if (mTessDebugMode == 2)
			rockPso = mDeferredGeoWirePSO.Get();

		cmd->SetPipelineState(rockPso);
		ObjectConstants rockCb = mSharedConstants;
		rockCb.DispScale = 0.045f;
		rockCb.MinTess = 1.0f;
		rockCb.MaxTess = 5.0f;
		rockCb.TessNear = mRockTessNear;
		rockCb.TessFar = mRockTessFar;
		rockCb.DebugMode = static_cast<float>(mTessDebugMode);

		for (uint32_t visIdx : mMeshInstances)
		{
			const SceneInstance& inst = mInstances[visIdx];
			drawSubmeshes(
				*mRockGeo,
				mRockSubmeshes,
				XMLoadFloat4x4(&inst.World),
				D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST,
				&rockCb);
		}

		if (mBillboardLodEnabled && !mBillboardInstances.empty())
		{
			BillboardMaterialConstants bbMat{};
			XMMATRIX invView = XMMatrixInverse(nullptr, view);
			XMStoreFloat4x4(&bbMat.ViewProj, XMMatrixTranspose(view * proj));
			XMStoreFloat3(&bbMat.CameraRight, XMVector3Normalize(invView.r[0]));
			XMStoreFloat3(&bbMat.CameraUp, XMVector3Normalize(invView.r[1]));
			bbMat.EyePosW = mCameraPos;
			bbMat.MatKd = mRockBillboardMaterial.Kd;
			bbMat.HasDiffuseTexture = mRockBillboardMaterial.HasDiffuseTexture ? 1.0f : 0.0f;
			bbMat.MatRoughness = mRockBillboardMaterial.Roughness;
			bbMat.MatMetallic = mRockBillboardMaterial.Metallic;
			bbMat.HasRmTexture = mRockBillboardMaterial.HasRmTexture ? 1.0f : 0.0f;
			bbMat.MatNsFallback = mRockBillboardMaterial.NsFallback;

			mDistantBillboards.DrawGBuffer(
				cmd,
				mBillboardInstances,
				bbMat,
				mSrvHeap.Get(),
				mBillboardInstanceSrvIndex,
				static_cast<UINT>(mRockBillboardSrvBase),
				mCbvSrvUavDescriptorSize);

			cmd->SetGraphicsRootSignature(mRootSignature.Get());
		}
	}
}

void ObjTexturesDemoApp::RunDeferredLightingPass()
{
	ID3D12GraphicsCommandList* const cmd = mCommandList.Get();

	mRenderer.TransitionGbufferToPixelShader(cmd);

	const bool useSceneRt = mRenderer.UsesSceneColorTarget();
	D3D12_CPU_DESCRIPTOR_HANDLE litRtv = CurrentBackBufferView();
	if (useSceneRt)
	{
		litRtv = mRenderer.SceneColorRtv();
	}
	else
	{
		CD3DX12_RESOURCE_BARRIER toRt = CD3DX12_RESOURCE_BARRIER::Transition(
			CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET);
		cmd->ResourceBarrier(1u, &toRt);
	}

	cmd->OMSetRenderTargets(1u, &litRtv, false, nullptr);
	cmd->ClearRenderTargetView(litRtv, Colors::Black, 0, nullptr);

	mRenderer.UpdateLightingFrameConstants(
		md3dDevice.Get(),
		mCameraPos,
		mDirLightW,
		XMFLOAT3(1.f, 0.98f, 0.92f),
		kDirLightIntensity);

	mRenderer.SetLightingPipeline(cmd);
	ID3D12DescriptorHeap* heaps[] = {mSrvHeap.Get()};
	cmd->SetDescriptorHeaps(1u, heaps);
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
