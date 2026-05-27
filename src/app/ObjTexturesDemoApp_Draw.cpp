// =============================================================================
// ObjTexturesDemoApp_Draw.cpp — запись команд GPU за один кадр Lab 3
// =============================================================================

#include "ObjTexturesDemoApp.h"

#include "../rendering/GBuffer.h"

#include <DirectXColors.h>

// Точка входа рендера: три последовательных этапа на command list
void ObjTexturesDemoApp::Draw(const FrameTimer&)
{
	StartDeferredFrameRecording();  // сброс allocator, привязка G-buffer, clear
	RunDeferredGeometryPass();      // Lab 3: tess + disp + normal → G-buffer
	RunDeferredLightingPass();      // Lab 2: чтение G-buffer → экран
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
	cmd->SetDescriptorHeaps(static_cast<UINT>(_countof(heaps)), heaps); // heap с текстурами и G-buffer SRV

	mRenderer.TransitionGbufferToRenderTarget(cmd);        // G-buffer RT: PS-readable → writable

	GBuffer* const gb = mRenderer.GetGBuffer();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsvGb = gb->DsvCpu(); // depth buffer G-buffer
	D3D12_CPU_DESCRIPTOR_HANDLE rtvMrt[GBuffer::kRtCount]{};
	for (UINT i = 0u; i < GBuffer::kRtCount; ++i)
		rtvMrt[i] = gb->RtvCpu(i);                       // 4 color targets: albedo, normal, pos, mat

	cmd->OMSetRenderTargets(GBuffer::kRtCount, rtvMrt, false, &dsvGb); // MRT + depth

	static const float kClearRt[4] = {0.f, 0.f, 0.f, 0.f};
	for (UINT i = 0u; i < GBuffer::kRtCount; ++i)
		cmd->ClearRenderTargetView(rtvMrt[i], kClearRt, 0, nullptr); // чёрный G-buffer

	cmd->ClearDepthStencilView(dsvGb, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0u, 0, nullptr); // z = 1
}

void ObjTexturesDemoApp::RunDeferredGeometryPass()
{
	ID3D12GraphicsCommandList* const cmd = mCommandList.Get();

	const bool wireframe = (mTessDebugMode == 2);          // debug: каркас тесселированных треугольников
	cmd->SetPipelineState(
		wireframe ? mDeferredGeoWirePSO.Get() : mDeferredGeoPSO.Get()); // VS+HS+DS+PS tess
	cmd->SetGraphicsRootSignature(mRootSignature.Get()); // b0=CBV, table=t0,t1,t2

	const D3D12_VERTEX_BUFFER_VIEW vbv = mModelGeo->VertexBufferView();
	const D3D12_INDEX_BUFFER_VIEW ibv = mModelGeo->IndexBufferView();
	cmd->IASetVertexBuffers(0u, 1u, &vbv);                 // Pos, Normal, TexC
	cmd->IASetIndexBuffer(&ibv);
	// Каждые 3 индекса = 1 патч; GPU вызовет VS×3, Hull, Tessellator, Domain, PS
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

	const UINT srvIncr = mCbvSrvUavDescriptorSize;       // размер одного descriptor в heap
	const CD3DX12_GPU_DESCRIPTOR_HANDLE srvBase{mSrvHeap->GetGPUDescriptorHandleForHeapStart()};

	const XMMATRIX view = XMLoadFloat4x4(&mView);
	const XMMATRIX proj = XMLoadFloat4x4(&mProj);
	UINT cbSlot = 0u;

	for (uint32_t instIdx : mVisibleInstances)
	{
		const SceneInstance& inst = mInstances[instIdx];
		const XMMATRIX world = XMLoadFloat4x4(&inst.World);
		const XMMATRIX wvp = world * view * proj;

		for (const DrawSubmesh& sm : mDrawSubmeshes)
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
		XMFLOAT3(1.f, 0.f, 0.f),
		1.0f);

	mRenderer.SetLightingPipeline(cmd);                      // deferred_lighting.hlsl
	cmd->SetGraphicsRootConstantBufferView(0u, mRenderer.LightingCb().Resource()->GetGPUVirtualAddress());

	const UINT srvIncr = mCbvSrvUavDescriptorSize;
	const CD3DX12_GPU_DESCRIPTOR_HANDLE lightSrv =
		mRenderer.LightingSrvGpuStart(mSrvHeap.Get(), mDeferredSrvHeapBase, srvIncr);
	cmd->SetGraphicsRootDescriptorTable(1u, lightSrv);     // SRV: 4×G-buffer + lights

	cmd->IASetVertexBuffers(0u, 0u, nullptr);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->DrawInstanced(3u, 1u, 0u, 0u);                    // fullscreen triangle

	CD3DX12_RESOURCE_BARRIER toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);
	cmd->ResourceBarrier(1u, &toPresent);
}

void ObjTexturesDemoApp::SubmitCommandListPresentAndFlush()
{
	ThrowIfFailed(mCommandList->Close());                  // закончить запись команд

	ID3D12CommandList* lists[] = {mCommandList.Get()};
	mCommandQueue->ExecuteCommandLists(static_cast<UINT>(_countof(lists)), lists); // GPU старт

	ThrowIfFailed(mSwapChain->Present(0u, 0u));            // показать кадр
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
	FlushCommandQueue();                                   // дождаться GPU (учебный пример)
}
