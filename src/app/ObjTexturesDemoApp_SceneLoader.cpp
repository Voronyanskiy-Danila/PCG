// =============================================================================
// ObjTexturesDemoApp_SceneLoader.cpp — загрузка сцены Lab 3
// =============================================================================
//
// Rock 07 (Poly Haven, CC0):
//   content/models/rock_07/rock_07.obj + .mtl + textures/*_diff|nor|disp*.jpg
//
// На каждый материал в descriptor heap резервируются 3 подряд SRV:
//   [base+0] diffuse  → shader t0
//   [base+1] normal    → shader t1
//   [base+2] displacement → shader t2 (domain shader)
//
// Слот 0 — white.dds (fallback, если у submesh нет материала).
// После загрузки: ComputeSceneFit — центр, масштаб ~10 единиц, стартовая камера.
//
// Ассеты: powershell -File tools/download_rock07.ps1
// =============================================================================

#include "ObjTexturesDemoApp.h"

#include "../importers/Importer_Image_DirectXTex.h"
#include "../importers/Importer_Wavefront_ObjMtl.h"
#include "../math/BoundingBox.h"
#include "../math/SceneFit.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace
{
	constexpr int kInstanceGridX = 20;
	constexpr int kInstanceGridZ = 10;
	constexpr float kInstanceSpacing = 1.1f;

	// Связывает submesh из OBJ с индексом SRV и флагами текстур для ObjectConstants
	std::vector<DrawSubmesh> BuildDrawSubmeshes(
		const ObjMeshData& data,
		const std::unordered_map<std::string, int>& matToSrvBase)
	{
		std::vector<DrawSubmesh> out;
		out.reserve(data.Submeshes.size());
		for (const auto& sm : data.Submeshes)
		{
			DrawSubmesh d{};
			d.StartIndexLocation = sm.StartIndexLocation;
			d.IndexCount = sm.IndexCount;
			auto itMat = data.Materials.find(sm.MaterialName);
			if (itMat == data.Materials.end())
			{
				d.MaterialSrvBase = 0;
				out.push_back(d);
				continue;
			}
			const MtlMaterial& m = itMat->second;
			d.Kd = m.Kd;
			d.Ks = m.Ks;
			d.Ns = m.Ns;
			d.HasDiffuseTexture = !m.DiffuseTexturePath.empty();
			d.HasNormalTexture = !m.NormalTexturePath.empty();
			auto tp = matToSrvBase.find(sm.MaterialName);
			d.MaterialSrvBase = (tp != matToSrvBase.end()) ? tp->second : 0;
			out.push_back(d);
		}
		return out;
	}

}

// Возвращает map: имя материала → индекс первого SRV (diffuse) в heap
std::unordered_map<std::string, int> ObjTexturesDemoApp::LoadMaterialTextureSets(
	const ObjMeshData& data,
	UINT& nextSlot)
{
	std::unordered_map<std::string, int> matToSrvBase;

	for (const auto& kv : data.Materials)
	{
		const MtlMaterial& m = kv.second;
		matToSrvBase[kv.first] = static_cast<int>(nextSlot);

		const wchar_t* diffPath =
			m.DiffuseTexturePath.empty() ? L"content/models/white.dds" : m.DiffuseTexturePath.c_str();
		const wchar_t* normPath =
			m.NormalTexturePath.empty() ? L"content/models/white.dds" : m.NormalTexturePath.c_str();
		const wchar_t* dispPath =
			m.DisplacementTexturePath.empty() ? L"content/models/white.dds" : m.DisplacementTexturePath.c_str();

		LoadTextureToSrvSlot(nextSlot + 0u, diffPath);
		LoadTextureToSrvSlot(nextSlot + 1u, normPath);
		LoadTextureToSrvSlot(nextSlot + 2u, dispPath);
		nextSlot += 3u;
	}

	return matToSrvBase;
}

void ObjTexturesDemoApp::LoadTextureToSrvSlot(UINT heapIndex, const wchar_t* path)
{
	HRESULT hr = LoadTextureImageFromFile12(
		md3dDevice.Get(),
		mCommandList.Get(),
		path,
		mTextureGPU[heapIndex],
		mTextureUploads[heapIndex]);
	if (FAILED(hr))
	{
		hr = LoadTextureImageFromFile12(
			md3dDevice.Get(),
			mCommandList.Get(),
			L"content/models/white.dds",
			mTextureGPU[heapIndex],
			mTextureUploads[heapIndex]);
	}
	ThrowIfFailed(hr);
	CreateSrvForTexture(static_cast<int>(heapIndex), mTextureGPU[heapIndex].Get());
}

void ObjTexturesDemoApp::LoadModelAndTextures()
{
	std::wstring err;
	ObjMeshData rockData;
	ObjMeshData sponzaData;
	const wchar_t* kRockObjPath = L"content/models/rock_07/rock_07.obj";
	const wchar_t* kSponzaObjPath = L"content/models/sponza/sponza.obj";

	if (!LoadWavefrontObj(kRockObjPath, rockData, err))
	{
		std::wstring msg = err;
		msg += L"\n\nExpected: content/models/rock_07/rock_07.obj (+ .mtl, textures/).";
		msg += L"\nRun: powershell -File tools/download_rock07.ps1";
		throw DxException(E_FAIL, msg, AnsiToWString(__FILE__), __LINE__);
	}
	if (!LoadWavefrontObj(kSponzaObjPath, sponzaData, err))
	{
		std::wstring msg = err;
		msg += L"\n\nExpected: content/models/sponza/sponza.obj (+ .mtl, textures/).";
		throw DxException(E_FAIL, msg, AnsiToWString(__FILE__), __LINE__);
	}

	const UINT materialSlots =
		static_cast<UINT>((rockData.Materials.size() + sponzaData.Materials.size()) * 3u);
	// После материалов в heap идут SRV G-buffer + structured buffer огней (RenderingSystem)
	mDeferredSrvHeapBase = 1u + materialSlots;
	const UINT srvCount = mDeferredSrvHeapBase + mRenderer.DeferredSrvDescriptorsNeeded();
	BuildDescriptorHeaps(srvCount);
	BuildConstantBuffers();

	mTextureGPU.assign(srvCount, {});
	mTextureUploads.assign(srvCount, {});

	LoadTextureToSrvSlot(0u, L"content/models/white.dds");

	UINT nextSlot = 1u;
	const std::unordered_map<std::string, int> rockMatToSrvBase = LoadMaterialTextureSets(rockData, nextSlot);
	const std::unordered_map<std::string, int> sponzaMatToSrvBase = LoadMaterialTextureSets(sponzaData, nextSlot);

	mRockGeo = BuildModelGeometry(rockData, "RockModel");
	mSceneGeo = BuildModelGeometry(sponzaData, "SponzaModel");
	mRockSubmeshes = BuildDrawSubmeshes(rockData, rockMatToSrvBase);
	mSceneSubmeshes = BuildDrawSubmeshes(sponzaData, sponzaMatToSrvBase);

	const Aabb sponzaLocalBounds = ComputeMeshLocalBounds(sponzaData);
	mSponzaWorldBounds = sponzaLocalBounds;
	mSceneWorldBounds = sponzaLocalBounds;
	mRenderer.SetParticleEmitter({
		(sponzaLocalBounds.Min.x + sponzaLocalBounds.Max.x) * 0.5f,
		(sponzaLocalBounds.Min.y + sponzaLocalBounds.Max.y) * 0.5f,
		(sponzaLocalBounds.Min.z + sponzaLocalBounds.Max.z) * 0.5f
	});

	// Масштаб камня под размер Sponza (чтобы не перекрывать целые арки/стены).
	const Aabb rockLocalBounds = ComputeMeshLocalBounds(rockData);
	const float rockExtent = (std::max)(
		(std::max)(rockLocalBounds.Max.x - rockLocalBounds.Min.x, rockLocalBounds.Max.y - rockLocalBounds.Min.y),
		rockLocalBounds.Max.z - rockLocalBounds.Min.z);
	const float sponzaExtent = (std::max)(
		(std::max)(sponzaLocalBounds.Max.x - sponzaLocalBounds.Min.x, sponzaLocalBounds.Max.y - sponzaLocalBounds.Min.y),
		sponzaLocalBounds.Max.z - sponzaLocalBounds.Min.z);
	const float rockTarget = (std::max)(2.0f, sponzaExtent * 0.03f);
	const float rockScale = (rockExtent > 1e-5f) ? (rockTarget / rockExtent) : 1.0f;

	const XMVECTOR rockCenter = XMVectorSet(
		(rockLocalBounds.Min.x + rockLocalBounds.Max.x) * 0.5f,
		(rockLocalBounds.Min.y + rockLocalBounds.Max.y) * 0.5f,
		(rockLocalBounds.Min.z + rockLocalBounds.Max.z) * 0.5f,
		1.0f);
	const float sponzaCenterX = (sponzaLocalBounds.Min.x + sponzaLocalBounds.Max.x) * 0.5f;
	const float sponzaCenterZ = (sponzaLocalBounds.Min.z + sponzaLocalBounds.Max.z) * 0.5f;
	const float sponzaFloorY = sponzaLocalBounds.Min.y;

	const XMMATRIX rockBase =
		XMMatrixTranslation(-XMVectorGetX(rockCenter), -XMVectorGetY(rockCenter), -XMVectorGetZ(rockCenter)) *
		XMMatrixScaling(rockScale, rockScale, rockScale) *
		XMMatrixTranslation(sponzaCenterX, sponzaFloorY + 0.15f, sponzaCenterZ);
	XMFLOAT4X4 rockBaseWorld = MathUtils::Identity4x4();
	XMStoreFloat4x4(&rockBaseWorld, rockBase);
	mMeshLocalBounds = rockLocalBounds;
	BuildSceneInstances(rockBaseWorld, rockLocalBounds);
	FitCameraToScene();
}

void ObjTexturesDemoApp::BuildSceneInstances(const XMFLOAT4X4& baseWorld, const Aabb& localBounds)
{
	const XMMATRIX base = XMLoadFloat4x4(&baseWorld);
	mInstances.clear();
	mInstances.reserve(static_cast<size_t>(kInstanceGridX * kInstanceGridZ));

	for (int iz = 0; iz < kInstanceGridZ; ++iz)
	{
		for (int ix = 0; ix < kInstanceGridX; ++ix)
		{
			const float ox = (static_cast<float>(ix) - (kInstanceGridX - 1) * 0.5f) * kInstanceSpacing;
			const float oz = (static_cast<float>(iz) - (kInstanceGridZ - 1) * 0.5f) * kInstanceSpacing;
			const float yaw = static_cast<float>((ix * 17 + iz * 31) % 360) * (XM_PI / 180.f);

			const XMMATRIX world =
				XMMatrixRotationY(yaw) * XMMatrixTranslation(ox, 0.f, oz) * base;

			SceneInstance inst{};
			XMStoreFloat4x4(&inst.World, world);
			inst.WorldBounds = TransformAabb(localBounds, world);
			mInstances.push_back(inst);
		}
	}

	mInstanceCount = static_cast<UINT>(mInstances.size());

	for (const SceneInstance& inst : mInstances)
		mSceneWorldBounds.Merge(inst.WorldBounds);

	BuildSceneOctree();
}

void ObjTexturesDemoApp::BuildSceneOctree()
{
	mOctreeItems.clear();
	mOctreeItems.reserve(mInstances.size());
	for (uint32_t i = 0; i < static_cast<uint32_t>(mInstances.size()); ++i)
	{
		OctreeItem item{};
		item.Index = i;
		item.Bounds = mInstances[i].WorldBounds;
		mOctreeItems.push_back(item);
	}
	mOctree.Build(mOctreeItems);
}

void ObjTexturesDemoApp::FitCameraToScene()
{
	const Aabb& fit = mSponzaWorldBounds.IsValid() ? mSponzaWorldBounds : mSceneWorldBounds;
	const float spanX = fit.Max.x - fit.Min.x;
	const float spanZ = fit.Max.z - fit.Min.z;
	const float spanY = (std::max)(fit.Max.y - fit.Min.y, 1.0f);
	const float sceneSpan = (std::max)((std::max)(spanX, spanZ), 1.0f);
	const float dist = (std::min)((std::max)(sceneSpan * 0.38f, 28.0f), 85.0f);

	const float cx = (fit.Min.x + fit.Max.x) * 0.5f;
	const float cz = (fit.Min.z + fit.Max.z) * 0.5f;

	mCameraPos = {cx, fit.Min.y + spanY * 0.35f + 6.0f, cz - dist};
	mYaw = 0.f;
	mPitch = 0.12f;
	mSkipNextMouseLook = true;
}
