#pragma once

#include "../math/MathUtils.h"
#include "../rendering/d3d12/D3d12_GpuUploadBuffer.h"
#include "../rendering/RenderingSystem.h"
#include "../importers/Importer_Wavefront_ObjMtl.h"
#include "D3d12AppBase.h"

#include <vector>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct Vertex
{
	XMFLOAT3 Pos;
	XMFLOAT3 Normal;
	XMFLOAT2 TexC;
};

struct ObjectConstants
{
	XMFLOAT4X4 World = MathUtils::Identity4x4();
	XMFLOAT4X4 WorldInvTranspose = MathUtils::Identity4x4();
	XMFLOAT4X4 WorldViewProj = MathUtils::Identity4x4();

	XMFLOAT3 EyePosW = {0.0f, 0.0f, 0.0f};
	float SpecPower = 64.0f;

	XMFLOAT3 LightDirW = {0.577f, -0.577f, 0.577f};
	float AmbientK = 0.15f;

	XMFLOAT3 LightColor = {1.0f, 1.0f, 1.0f};
	float _pad0 = 0.0f;

	XMFLOAT3 MatKa = {0.2f, 0.2f, 0.2f};
	float HasDiffuseTexture = 0.0f;

	XMFLOAT3 MatKd = {0.8f, 0.8f, 0.8f};
	float MatNs = 32.0f;

	XMFLOAT3 MatKs = {0.2f, 0.2f, 0.2f};
	float _pad1 = 0.0f;

	XMFLOAT2 UvScale = {1.0f, 1.0f};
	XMFLOAT2 UvScroll = {0.0f, 0.0f};
	// .x = время (с); float4 — как в HLSL, без «дырок» в CB.
	XMFLOAT4 UvAnimParams = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct DrawSubmesh
{
	UINT StartIndexLocation = 0;
	UINT IndexCount = 0;
	int DiffuseSrvIndex = 0;
	bool HasDiffuseTexture = false;
	XMFLOAT3 Ka = {0.2f, 0.2f, 0.2f};
	XMFLOAT3 Kd = {0.8f, 0.8f, 0.8f};
	XMFLOAT3 Ks = {0.2f, 0.2f, 0.2f};
	float Ns = 32.0f;
};

class ObjTexturesDemoApp : public D3d12AppBase
{
public:
	ObjTexturesDemoApp(HINSTANCE hInstance);
	ObjTexturesDemoApp(const ObjTexturesDemoApp& rhs) = delete;
	ObjTexturesDemoApp& operator=(const ObjTexturesDemoApp& rhs) = delete;
	~ObjTexturesDemoApp();

	virtual bool Initialize() override;

private:
	virtual void OnResize() override;
	virtual void Update(const FrameTimer& gt) override;
	virtual void Draw(const FrameTimer& gt) override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y) override;
	virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

	void BuildDescriptorHeaps(UINT srvCount);
	void BuildConstantBuffers();
	void BuildRootSignature();
	void BuildGeometryInputLayout();
	void LoadModelAndTextures();
	void FitWorldAndCameraToMesh(const ObjMeshData& data);
	void BuildModelGeometry(const ObjMeshData& data);
	void CreateSrvForTexture(int heapIndex, ID3D12Resource* tex);
	void BuildDeferredGeometryPipeline();
	void RefreshDeferredSrvs();
	void SetupSceneLights();

private:
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mSrvHeap = nullptr;

	std::unique_ptr<GpuUploadBuffer<ObjectConstants>> mObjectCB = nullptr;

	std::unique_ptr<MeshGeometry> mModelGeo = nullptr;
	std::vector<DrawSubmesh> mDrawSubmeshes;

	std::vector<ComPtr<ID3D12Resource>> mTextureGPU;
	std::vector<ComPtr<ID3D12Resource>> mTextureUploads;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	ComPtr<ID3D12PipelineState> mDeferredGeoPSO = nullptr;

	RenderingSystem mRenderer{};
	UINT mDeferredSrvHeapBase = 0;
	std::vector<GpuLight> mSceneLights;

	XMFLOAT4X4 mWorld = MathUtils::Identity4x4();
	XMFLOAT4X4 mView = MathUtils::Identity4x4();
	XMFLOAT4X4 mProj = MathUtils::Identity4x4();

	// Свободная камера: yaw (вокруг Y) и pitch (наклон вверх/вниз), радианы.
	float mYaw = 0.0f;
	float mPitch = 0.0f;

	XMFLOAT3 mCameraPos{};

	POINT mLastMousePos{};

	bool mKeyW = false;
	bool mKeyA = false;
	bool mKeyS = false;
	bool mKeyD = false;
	bool mKeyAscend = false;   // Space — вверх по миру
	bool mKeyDescend = false;  // Ctrl — вниз по миру

	float mCameraSpeed = 8.0f;
	float mMouseSensitivity = 0.0022f;

	XMFLOAT2 mUvScale = {1.08f, 1.08f};
	XMFLOAT2 mUvScroll = {0.018f, 0.014f};

	ObjectConstants mSharedConstants{};
};

