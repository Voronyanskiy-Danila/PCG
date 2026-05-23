// =============================================================================
// ObjTexturesDemoApp.h — приложение Lab 3 (deferred + hardware tessellation)
// =============================================================================
//
// Требования лабораторной:
//   1) Модель с normal map и displacement map (Rock 07, Poly Haven)
//   2) Тессиляция + сдвиг по displacement (HS/DS в deferred_tessellation.hlsl)
//   3) Normal map в G-buffer → освещение в deferred_lighting.hlsl
//   4) LOD тесселяции от расстояния до камеры (PatchConstantHS)
//
// Управление: WASD + мышь — камера; T — цикл debug-режимов (mTessDebugMode 0..3).
//
// Связанные файлы:
//   ObjTexturesDemoApp.cpp          — init, PSO tess, Update, клавиша T
//   ObjTexturesDemoApp_Draw.cpp     — G-buffer pass + lighting pass
//   ObjTexturesDemoApp_SceneLoader.cpp — OBJ/MTL, 3 текстуры на материал
//   content/shaders/deferred_tessellation.hlsl
// =============================================================================

#pragma once

#include "../math/MathUtils.h"
#include "../rendering/d3d12/D3d12_GpuUploadBuffer.h"
#include "../rendering/RenderingSystem.h"
#include "../importers/Importer_Wavefront_ObjMtl.h"
#include "D3d12AppBase.h"

#include <string>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// Вершина меша: совпадает с VSInput в deferred_tessellation.hlsl
struct Vertex
{
	XMFLOAT3 Pos;
	XMFLOAT3 Normal;
	XMFLOAT2 TexC;
};

// Constant buffer b0 — байт-в-байт с cbuffer ObjectCB в deferred_tessellation.hlsl
struct ObjectConstants
{
	XMFLOAT4X4 World = MathUtils::Identity4x4();
	XMFLOAT4X4 WorldInvTranspose = MathUtils::Identity4x4();
	XMFLOAT4X4 WorldViewProj = MathUtils::Identity4x4();

	XMFLOAT3 EyePosW = {0.0f, 0.0f, 0.0f}; // для TessFactorFromWorldPos в hull
	float _pad0 = 0.0f;

	XMFLOAT3 MatKd = {0.8f, 0.8f, 0.8f};
	float HasDiffuseTexture = 0.0f;

	XMFLOAT3 MatKs = {0.2f, 0.2f, 0.2f};
	float MatNs = 32.0f;

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
	float _pad2 = 0.0f;
};

// Один draw call: диапазон индексов + материал (3 SRV подряд в heap)
struct DrawSubmesh
{
	UINT StartIndexLocation = 0;
	UINT IndexCount = 0;
	int MaterialSrvBase = 0;   // индекс первого SRV: diffuse (t0), +1 normal, +2 disp
	bool HasDiffuseTexture = false;
	bool HasNormalTexture = false;
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
	void BuildModelGeometry(const ObjMeshData& data);
	void CreateSrvForTexture(int heapIndex, ID3D12Resource* tex);
	void LoadTextureToSrvSlot(UINT heapIndex, const wchar_t* path);
	std::unordered_map<std::string, int> LoadMaterialTextureSets(const ObjMeshData& data);
	void BuildDeferredGeometryPipeline();
	void UpdateWindowCaption();
	void RefreshDeferredSrvs();
	void SetupSceneLights();

	XMVECTOR CameraForwardNormalized() const;
	void UpdateCameraAttachedSpotLight();

	// Два прохода deferred (Lab 2) + tess в геометрии (Lab 3)
	void StartDeferredFrameRecording();
	void RunDeferredGeometryPass();
	void RunDeferredLightingPass();
	void SubmitCommandListPresentAndFlush();

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mSrvHeap = nullptr;

	std::unique_ptr<GpuUploadBuffer<ObjectConstants>> mObjectCB = nullptr;

	std::unique_ptr<MeshGeometry> mModelGeo = nullptr;
	std::vector<DrawSubmesh> mDrawSubmeshes;

	std::vector<ComPtr<ID3D12Resource>> mTextureGPU;
	std::vector<ComPtr<ID3D12Resource>> mTextureUploads;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// PSO: VS+HS+DS+PS, PATCH topology; wire — debug режим 2
	ComPtr<ID3D12PipelineState> mDeferredGeoPSO = nullptr;
	ComPtr<ID3D12PipelineState> mDeferredGeoWirePSO = nullptr;

	int mTessDebugMode = 0;

	RenderingSystem mRenderer{};
	UINT mDeferredSrvHeapBase = 0; // смещение SRV G-buffer в общем heap
	std::vector<GpuLight> mSceneLights;

	XMFLOAT4X4 mWorld = MathUtils::Identity4x4();
	XMFLOAT4X4 mView = MathUtils::Identity4x4();
	XMFLOAT4X4 mProj = MathUtils::Identity4x4();

	float mYaw = 0.0f;
	float mPitch = 0.0f;
	XMFLOAT3 mCameraPos{};
	XMFLOAT3 mDirLightW{0.577f, -0.577f, 0.577f};

	POINT mLastMousePos{};

	bool mKeyW = false;
	bool mKeyA = false;
	bool mKeyS = false;
	bool mKeyD = false;
	bool mKeyAscend = false;
	bool mKeyDescend = false;

	float mCameraSpeed = 8.0f;
	float mMouseSensitivity = 0.0022f;

	ObjectConstants mSharedConstants{}; // заполняется в Update, дополняется per-submesh в Draw
};
