#include "ObjTexturesDemoApp.h"

#include "../rendering/GBuffer.h"

#include <DirectXColors.h>

void ObjTexturesDemoApp::Draw(const FrameTimer&)
{
	StartDeferredFrameRecording();
	RunDeferredGeometryPass();
	RunDeferredLightingPass();
	SubmitCommandListPresentAndFlush();
}

void ObjTexturesDemoApp::StartDeferredFrameRecording()
{
	ThrowIfFailed(mDirectCmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	ID3D12GraphicsCommandList* const cmd = mCommandList.Get();
	cmd->RSSetViewports(1, &mScreenViewport);
	cmd->RSSetScissorRects(1, &mScissorRect);

	ID3D12DescriptorHeap* heaps[] = {mSrvHeap.Get()};
	cmd->SetDescriptorHeaps(static_cast<UINT>(_countof(heaps)), heaps);

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

void ObjTexturesDemoApp::RunDeferredGeometryPass()
{
	ID3D12GraphicsCommandList* const cmd = mCommandList.Get();
	cmd->SetPipelineState(mDeferredGeoPSO.Get());
	cmd->SetGraphicsRootSignature(mRootSignature.Get());

	const D3D12_VERTEX_BUFFER_VIEW vbv = mModelGeo->VertexBufferView();
	const D3D12_INDEX_BUFFER_VIEW ibv = mModelGeo->IndexBufferView();
	cmd->IASetVertexBuffers(0u, 1u, &vbv);
	cmd->IASetIndexBuffer(&ibv);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	const UINT srvIncr = mCbvSrvUavDescriptorSize;
	const CD3DX12_GPU_DESCRIPTOR_HANDLE srvBase{mSrvHeap->GetGPUDescriptorHandleForHeapStart()};

	for (const DrawSubmesh& sm : mDrawSubmeshes)
	{
		ObjectConstants per = mSharedConstants;
		per.MatKa = sm.Ka;
		per.MatKd = sm.Kd;
		per.MatKs = sm.Ks;
		per.MatNs = sm.Ns;
		per.HasDiffuseTexture = sm.HasDiffuseTexture ? 1.f : 0.f;

		mObjectCB->CopyData(0u, per);
		cmd->SetGraphicsRootConstantBufferView(0u, mObjectCB->Resource()->GetGPUVirtualAddress());

		CD3DX12_GPU_DESCRIPTOR_HANDLE texH{srvBase};
		texH.Offset(sm.DiffuseSrvIndex, srvIncr);
		cmd->SetGraphicsRootDescriptorTable(1u, texH);

		cmd->DrawIndexedInstanced(sm.IndexCount, 1u, sm.StartIndexLocation, 0, 0u);
	}
}

void ObjTexturesDemoApp::RunDeferredLightingPass()
{
	ID3D12GraphicsCommandList* const cmd = mCommandList.Get();

	mRenderer.TransitionGbufferToPixelShader(cmd);

	CD3DX12_RESOURCE_BARRIER toRt = CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	cmd->ResourceBarrier(1u, &toRt);

	const D3D12_CPU_DESCRIPTOR_HANDLE bbRtv = CurrentBackBufferView();
	cmd->OMSetRenderTargets(1u, &bbRtv, false, nullptr);
	cmd->ClearRenderTargetView(bbRtv, Colors::Black, 0, nullptr);

	mRenderer.UpdateLightingFrameConstants(
		md3dDevice.Get(),
		mCameraPos,
		mSharedConstants.LightDirW,
		XMFLOAT3(1.f, 0.f, 0.f),
		1.0f);

	mRenderer.SetLightingPipeline(cmd);
	cmd->SetGraphicsRootConstantBufferView(0u, mRenderer.LightingCb().Resource()->GetGPUVirtualAddress());

	const UINT srvIncr = mCbvSrvUavDescriptorSize;
	const CD3DX12_GPU_DESCRIPTOR_HANDLE lightSrv =
		mRenderer.LightingSrvGpuStart(mSrvHeap.Get(), mDeferredSrvHeapBase, srvIncr);
	cmd->SetGraphicsRootDescriptorTable(1u, lightSrv);

	cmd->IASetVertexBuffers(0u, 0u, nullptr);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->DrawInstanced(3u, 1u, 0u, 0u);

	CD3DX12_RESOURCE_BARRIER toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);
	cmd->ResourceBarrier(1u, &toPresent);
}

void ObjTexturesDemoApp::SubmitCommandListPresentAndFlush()
{
	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* lists[] = {mCommandList.Get()};
	mCommandQueue->ExecuteCommandLists(static_cast<UINT>(_countof(lists)), lists);

	ThrowIfFailed(mSwapChain->Present(0u, 0u));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
	FlushCommandQueue();
}
