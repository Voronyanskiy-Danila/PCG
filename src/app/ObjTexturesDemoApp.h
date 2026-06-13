// =============================================================================
// ObjTexturesDemoApp.h — Lab 3 (tess) + Lab 5 (particles) + Lab 7 (post) + Lab 8 (PBR)
// =============================================================================
//
// Lab 3:
//   1) Rock 07 + normal/displacement maps
//   2) Tessellation + displacement (deferred_tessellation.hlsl)
//   3) Normal map → G-buffer → deferred_lighting.hlsl
//   4) LOD tess по расстоянию (PatchConstantHS)
// =============================================================================

#pragma once

#include "../math/BoundingBox.h"
#include "../math/Frustum.h"
#include "../scene/Octree.h"
#include "../math/MathUtils.h"
#include "../rendering/d3d12/D3d12_GpuUploadBuffer.h"
#include "../rendering/RenderingSystem.h"
#include "../importers/Importer_Wavefront_ObjMtl.h"
#include "D3d12AppBase.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

inline constexpr float kDirLightIntensity = 18.0f;
inline constexpr float kLocalLightIntensity = 28000.0f;
inline constexpr float kCameraSpotIntensityMul = 5.0f;
inline constexpr float kCameraSpotRange = 320.0f;
inline constexpr float kCameraSpotInnerAngleDeg = 22.0f;
inline constexpr float kCameraSpotOuterAngleDeg = 42.0f;

// Вершина меша: совпадает с VSInput в deferred_tessellation.hlsl
struct Vertex
{
	XMFLOAT3 Pos;
	XMFLOAT3 Normal;
	XMFLOAT2 TexC;
	XMFLOAT4 Tangent = {0.0f, 0.0f, 0.0f, 1.0f}; // xyz=tangent, w=bitangent handedness
};

// Constant buffer b0 — байт-в-байт с cbuffer ObjectCB в deferred_tessellation.hlsl
struct ObjectConstants
{
	XMFLOAT4X4 World = MathUtils::Identity4x4();
	XMFLOAT4X4 WorldInvTranspose = MathUtils::Identity4x4();
	XMFLOAT4X4 WorldViewProj = MathUtils::Identity4x4();

	XMFLOAT3 EyePosW = {0.0f, 0.0f, 0.0f};
	float _pad0 = 0.0f;

	XMFLOAT3 MatKd = {0.8f, 0.8f, 0.8f};
	float HasDiffuseTexture = 0.0f;

	float MatRoughness = 0.5f;
	float MatMetallic = 0.0f;
	float HasRmTexture = 0.0f;
	float MatNsFallback = 32.0f;

	XMFLOAT2 UvScale = {1.0f, 1.0f};
	XMFLOAT2 _pad1 = {0.0f, 0.0f};

	// Lab 3 — параметры domain/hull (значения по умолчанию под масштаб Rock 07)
	float DispScale = 0.04f;   // амплитуда displacement
	float MinTess = 1.0f;      // tess factor вдали
	float MaxTess = 24.0f;     // tess factor вблизи
	float TessNear = 5.0f;     // дистанция «полного» tess

	float TessFar = 22.0f;     // дистанция «минимального» tess
	float HasNormalTexture = 0.0f;
	float DebugMode = 0.0f;    // 0..3, см. клавишу T
	float NormalFlipY = 0.0f;  // OpenGL normal maps (Sponza *_ddn*)
};

// Один draw call: диапазон индексов + материал (4 SRV подряд в heap)
struct DrawSubmesh
{
	UINT StartIndexLocation = 0;
	UINT IndexCount = 0;
	int MaterialSrvBase = 0;   // diffuse t0, +1 normal, +2 disp, +3 ARM
	bool HasDiffuseTexture = false;
	bool HasNormalTexture = false;
	bool HasRmTexture = false;
	bool NormalFlipY = false;
	bool SkipNormalMap = false;
	XMFLOAT2 UvScale = {1.0f, 1.0f};
	XMFLOAT3 Kd = {0.8f, 0.8f, 0.8f};
	float Roughness = 0.5f;
	float Metallic = 0.0f;
	float NsFallback = 32.0f;
};

struct SceneInstance
{
	XMFLOAT4X4 World = MathUtils::Identity4x4();
	Aabb WorldBounds{};
};

class ObjTexturesDemoApp : public D3d12AppBase
{
public:
	static constexpr UINT kMaxObjectDrawCalls = 1024u;
	static constexpr float kCameraNearZ = 0.5f;
	static constexpr float kCameraFarZ = 5000.0f;
	static constexpr float kCameraFovYRad = 0.25f * DirectX::XM_PI;
	static constexpr float kShadowCullFovScale = 1.5f;

	ObjTexturesDemoApp(HINSTANCE hInstance);
	ObjTexturesDemoApp(const ObjTexturesDemoApp& rhs) = delete;
	ObjTexturesDemoApp& operator=(const ObjTexturesDemoApp& rhs) = delete;
	~ObjTexturesDemoApp();

	static void SetContentRoot(const std::filesystem::path& root);
	static const std::filesystem::path& ContentRoot();
	static std::wstring ResolveContentPath(const wchar_t* relativePath);

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
	void EnsureCerberusAssets();
	void EnsureRockAssets();
	std::unique_ptr<MeshGeometry> BuildModelGeometry(const ObjMeshData& data, const char* name);
	void CreateSrvForTexture(int heapIndex, ID3D12Resource* tex);
	void LoadTextureToSrvSlot(UINT heapIndex, const wchar_t* path);
	std::unordered_map<std::string, int> LoadMaterialTextureSets(const ObjMeshData& data, UINT& nextSlot);
	void BuildDeferredGeometryPipeline();
	void UpdateWindowCaption();
	void RefreshDeferredSrvs();
	void SetupSceneLights();
	void BuildSceneInstances(
		const Aabb& localBounds,
		float rockScale,
		float floorY,
		const XMFLOAT3& gridCenter,
		float instanceSpacing);
	void BuildSceneOctree();
	void FitCameraToScene();
	void UpdateVisibility();
	void UpdateShadowCasters();
	void CullInstancesLinear();
	UINT CountShadowDrawCalls() const;

	XMVECTOR CameraForwardNormalized() const;
	void UpdateCameraAttachedSpotLight();

	// Два прохода deferred (Lab 2) + tess в геометрии (Lab 3)
	void StartDeferredFrameRecording();
	void BindAndClearGBuffer();
	void RunShadowPass();
	void RunDeferredGeometryPass();
	void RunDeferredLightingPass();
	void SubmitCommandListPresentAndFlush();

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mSrvHeap = nullptr;

	std::unique_ptr<GpuUploadBuffer<ObjectConstants>> mObjectCB = nullptr;
	UINT mObjectCbElementSize = 0;

	std::unique_ptr<MeshGeometry> mRockGeo = nullptr;
	std::unique_ptr<MeshGeometry> mSceneGeo = nullptr;
	std::unique_ptr<MeshGeometry> mPropGeo = nullptr;
	std::vector<SceneInstance> mInstances;
	Aabb mMeshLocalBounds{};
	Aabb mSponzaWorldBounds{};
	XMFLOAT3 mCourtyardAnchor{};
	float mRockClusterCenterY = 0.f;
	Aabb mShadowSceneBounds{};
	Aabb mSceneWorldBounds{};
	std::vector<uint32_t> mVisibleInstances;
	UINT mInstanceCount = 0;
	UINT mVisibleCount = 0;
	UINT mShadowDrawSlotsUsed = 0;
	UINT mShadowDrawCallsNeeded = 0;
	bool mShadowDrawOverflow = false;
	bool mGeometryDrawOverflow = false;
	bool mShadowDrawSponza = true;

	bool mFrustumCullingEnabled = true;
	bool mOctreeFrustumEnabled = false;
	Frustum mFrustum{};
	Octree mOctree{};
	std::vector<OctreeItem> mOctreeItems;
	std::vector<DrawSubmesh> mRockSubmeshes;
	std::vector<DrawSubmesh> mSceneSubmeshes;
	std::vector<DrawSubmesh> mPropSubmeshes;
	XMFLOAT4X4 mPropWorld = MathUtils::Identity4x4();
	Aabb mPropWorldBounds{};

	std::vector<ComPtr<ID3D12Resource>> mTextureGPU;
	std::vector<ComPtr<ID3D12Resource>> mTextureUploads;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// PSO: VS+HS+DS+PS PATCH (камни); solid TRIANGLE (Sponza/Cerberus)
	ComPtr<ID3D12PipelineState> mDeferredGeoPSO = nullptr;
	ComPtr<ID3D12PipelineState> mDeferredGeoRockPSO = nullptr;
	ComPtr<ID3D12PipelineState> mDeferredGeoWirePSO = nullptr;
	ComPtr<ID3D12PipelineState> mDeferredGeoSolidPSO = nullptr;
	ComPtr<ID3D12PipelineState> mDeferredGeoSolidNoCullPSO = nullptr;

	int mTessDebugMode = 0;

	RenderingSystem mRenderer{};
	UINT mDeferredSrvHeapBase = 0; // смещение SRV G-buffer в общем heap
	std::vector<GpuLight> mSceneLights;

	XMFLOAT4X4 mView = MathUtils::Identity4x4();
	XMFLOAT4X4 mProj = MathUtils::Identity4x4();

	float mYaw = 0.0f;
	float mPitch = 0.0f;
	XMFLOAT3 mCameraPos{};
	XMFLOAT3 mDirLightW{0.35f, -0.85f, 0.38f};

	POINT mLastMousePos{};
	bool mSkipNextMouseLook = false;

	bool mKeyW = false;
	bool mKeyA = false;
	bool mKeyS = false;
	bool mKeyD = false;
	bool mKeyAscend = false;
	bool mKeyDescend = false;
	bool mKeyBoost = false;

	float mCameraSpeed = 280.0f;
	float mCameraBoostMultiplier = 2.5f;
	float mMouseSensitivity = 0.0022f;
	float mDisplayFps = 0.0f;

	ObjectConstants mSharedConstants{}; // заполняется в Update, дополняется per-submesh в Draw

	static std::filesystem::path s_contentRoot;
};
