// =============================================================================
// ObjTexturesDemoApp_SceneLoader.cpp — загрузка сцены Lab 3
// =============================================================================
//
// Rock 07 (Poly Haven, CC0):
//   content/models/rock_07/rock_07.obj + .mtl + textures/*_diff|nor|disp*.jpg
//
// На каждый материал в descriptor heap резервируются 4 подряд SRV:
//   [base+0] diffuse  → shader t0
//   [base+1] normal    → shader t1
//   [base+2] displacement → shader t2
//   [base+3] ARM (AO/R/M) → shader t3 (Lab 8 PBR)
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
#include "../math/MathUtils.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace
{
	constexpr int kInstanceGridX = 8;
	constexpr int kInstanceGridZ = 6;
	constexpr float kRockClearanceAboveSponzaTop = 14.0f;

	std::wstring ToLowerWide(std::wstring s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
			return static_cast<wchar_t>(std::towlower(c));
		});
		return s;
	}

	void ApplyMaterialPipelineHints(MtlMaterial& m)
	{
		const std::wstring probe = ToLowerWide(m.DiffuseTexturePath + L"|" + m.NormalTexturePath);
		if (probe.find(L"sponza") != std::wstring::npos)
		{
			m.UvScale = {1.f, -1.f};
			m.SkipNormalMap = true;
		}
		if (probe.find(L"cerberus") != std::wstring::npos)
			m.UvScale = {1.f, -1.f};

		if (!m.NormalTexturePath.empty())
		{
			const std::wstring norm = ToLowerWide(m.NormalTexturePath);
			if (norm.find(L"_gl") != std::wstring::npos || norm.find(L"ddn") != std::wstring::npos)
				m.NormalFlipY = true;
			else if (norm.find(L"_dx") != std::wstring::npos || norm.find(L"cerberus_n") != std::wstring::npos)
				m.NormalFlipY = false;
		}
	}

	void ApplyPipelineHints(ObjMeshData& data)
	{
		for (auto& kv : data.Materials)
			ApplyMaterialPipelineHints(kv.second);
	}

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
			d.Roughness = m.RoughnessFactor;
			d.Metallic = m.MetallicFactor;
			d.NsFallback = m.Ns;
			d.HasDiffuseTexture = !m.DiffuseTexturePath.empty();
			d.HasNormalTexture = !m.NormalTexturePath.empty() && !m.SkipNormalMap;
			d.NormalFlipY = m.NormalFlipY;
			d.SkipNormalMap = m.SkipNormalMap;
			d.UvScale = m.UvScale;
			d.HasRmTexture =
				!m.RmTexturePath.empty() && std::filesystem::exists(std::filesystem::path(m.RmTexturePath));
			if (!d.HasRmTexture)
				d.Metallic = 0.f;
			else if (m.HasMetallicFactor)
				d.Metallic = m.MetallicFactor;
			else
				d.Metallic = 1.f;
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
		const wchar_t* rmPath =
			m.RmTexturePath.empty() ? L"content/models/white.dds" : m.RmTexturePath.c_str();

		LoadTextureToSrvSlot(nextSlot + 0u, diffPath);
		LoadTextureToSrvSlot(nextSlot + 1u, normPath);
		LoadTextureToSrvSlot(nextSlot + 2u, dispPath);
		LoadTextureToSrvSlot(nextSlot + 3u, rmPath);
		nextSlot += 4u;
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
		std::wstring msg = L"[PCG] Texture load failed, using white.dds fallback:\n";
		msg += path ? path : L"(null)";
		msg += L'\n';
		OutputDebugStringW(msg.c_str());
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

void ObjTexturesDemoApp::EnsureCerberusAssets()
{
	namespace fs = std::filesystem;
	const fs::path root = ContentRoot();
	if (root.empty())
		return;

	const auto runChecked = [](const wchar_t* cmd) {
		const int rc = _wsystem(cmd);
		if (rc != 0)
		{
			std::wstring msg = L"[PCG] EnsureCerberusAssets failed (code ";
			msg += std::to_wstring(rc);
			msg += L"): ";
			msg += cmd;
			msg += L"\n";
			OutputDebugStringW(msg.c_str());
		}
	};

	const fs::path cerberusDir = root / L"Stuff" / L"Cerberus_by_Andrew_Maximov";
	const fs::path texDir = cerberusDir / L"Textures";
	const fs::path pbrObj = cerberusDir / L"Cerberus_PBR.obj";
	const auto hasCoreTextures = [&]() {
		return fs::exists(texDir / L"Cerberus_A.jpg") &&
			fs::exists(texDir / L"Cerberus_N.jpg") &&
			fs::exists(texDir / L"Cerberus_R.jpg") &&
			fs::exists(texDir / L"Cerberus_M.jpg");
	};

	if (fs::exists(pbrObj) && hasCoreTextures() && fs::exists(texDir / L"Cerberus_ARM.jpg"))
		return;

	const fs::path zip = root / L"Stuff" / L"PBR models.zip";
	if (!hasCoreTextures() && fs::exists(zip))
	{
		const std::wstring cmd =
			L"powershell -NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath '" +
			zip.wstring() + L"' -DestinationPath '" + (root / L"Stuff").wstring() + L"' -Force\"";
		runChecked(cmd.c_str());
	}

	const fs::path baseObj = cerberusDir / L"Cerberus.obj";
	if (!fs::exists(baseObj))
	{
		const std::wstring cmd =
			L"powershell -NoProfile -Command \"Invoke-WebRequest -Uri "
			L"'https://raw.githubusercontent.com/mrdoob/three.js/r165/examples/models/obj/cerberus/Cerberus.obj' "
			L"-OutFile '" +
			baseObj.wstring() + L"' -UseBasicParsing\"";
		runChecked(cmd.c_str());
	}

	if (fs::exists(root / L"tools" / L"pack_cerberus_arm.py"))
		runChecked(L"py -3 tools/pack_cerberus_arm.py");
	if (fs::exists(root / L"tools" / L"prepare_cerberus_obj.py"))
		runChecked(L"py -3 tools/prepare_cerberus_obj.py");

	if ((!fs::exists(pbrObj) || !hasCoreTextures()) &&
		fs::exists(root / L"tools" / L"setup_cerberus.ps1"))
	{
		runChecked(L"powershell -NoProfile -ExecutionPolicy Bypass -File tools/setup_cerberus.ps1");
	}
}

void ObjTexturesDemoApp::EnsureRockAssets()
{
	namespace fs = std::filesystem;
	const fs::path root = ContentRoot();
	if (root.empty())
		return;

	const fs::path rockDir = root / L"content" / L"models" / L"rock_07";
	const fs::path diff = rockDir / L"textures" / L"rock_07_diff_1k.jpg";
	const fs::path obj = rockDir / L"rock_07.obj";
	if (fs::exists(obj) && fs::exists(diff))
		return;

	const fs::path script = root / L"tools" / L"download_rock07.ps1";
	if (fs::exists(script))
	{
		_wsystem(
			L"powershell -NoProfile -ExecutionPolicy Bypass -File tools/download_rock07.ps1");
	}
}

void ObjTexturesDemoApp::LoadModelAndTextures()
{
	std::wstring err;
	ObjMeshData rockData;
	ObjMeshData sponzaData;
	ObjMeshData cerberusData;
	const wchar_t* kRockObjPath = L"content/models/rock_07/rock_07.obj";
	const wchar_t* kSponzaObjPath = L"content/models/sponza/sponza.obj";
	const std::wstring kCerberusObjPath = ResolveContentPath(L"Stuff/Cerberus_by_Andrew_Maximov/Cerberus_PBR.obj");

	EnsureRockAssets();

	if (!LoadWavefrontObj(kRockObjPath, rockData, err))
	{
		std::wstring msg = err;
		msg += L"\n\nExpected: content/models/rock_07/rock_07.obj (+ .mtl, textures/).";
		msg += L"\nRun: powershell -File tools/download_rock07.ps1";
		throw DxException(E_FAIL, msg, AnsiToWString(__FILE__), __LINE__);
	}
	ApplyPipelineHints(rockData);
	if (!LoadWavefrontObj(kSponzaObjPath, sponzaData, err))
	{
		std::wstring msg = err;
		msg += L"\n\nExpected: content/models/sponza/sponza.obj (+ .mtl, textures/).";
		throw DxException(E_FAIL, msg, AnsiToWString(__FILE__), __LINE__);
	}
	ApplyPipelineHints(sponzaData);

	EnsureCerberusAssets();

	{
		namespace fs = std::filesystem;
		const fs::path texDir =
			ContentRoot() / L"Stuff" / L"Cerberus_by_Andrew_Maximov" / L"Textures";
		if (!fs::exists(texDir / L"Cerberus_A.jpg"))
		{
			std::wstring msg = L"Cerberus textures missing under Stuff/Cerberus_by_Andrew_Maximov/Textures/.";
			msg += L"\nRun: powershell -File tools/setup_cerberus.ps1";
			msg += L"\nOr extract Stuff/PBR models.zip";
			throw DxException(E_FAIL, msg, AnsiToWString(__FILE__), __LINE__);
		}
	}

	if (!LoadWavefrontObj(kCerberusObjPath.c_str(), cerberusData, err))
	{
		std::wstring msg = err;
		msg += L"\n\nExpected: Stuff/Cerberus_by_Andrew_Maximov/Cerberus_PBR.obj";
		msg += L"\nRun: powershell -File tools/setup_cerberus.ps1";
		throw DxException(E_FAIL, msg, AnsiToWString(__FILE__), __LINE__);
	}
	ApplyPipelineHints(cerberusData);

	const UINT materialSlots = static_cast<UINT>(
		(rockData.Materials.size() + sponzaData.Materials.size() + cerberusData.Materials.size()) * 4u);
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
	const std::unordered_map<std::string, int> cerberusMatToSrvBase = LoadMaterialTextureSets(cerberusData, nextSlot);

	mRockGeo = BuildModelGeometry(rockData, "RockModel");
	mSceneGeo = BuildModelGeometry(sponzaData, "SponzaModel");
	mPropGeo = BuildModelGeometry(cerberusData, "CerberusProp");
	mRockSubmeshes = BuildDrawSubmeshes(rockData, rockMatToSrvBase);
	mSceneSubmeshes = BuildDrawSubmeshes(sponzaData, sponzaMatToSrvBase);
	mPropSubmeshes = BuildDrawSubmeshes(cerberusData, cerberusMatToSrvBase);

	const Aabb sponzaLocalBounds = ComputeMeshLocalBounds(sponzaData);
	mSponzaWorldBounds = sponzaLocalBounds;
	mSceneWorldBounds = sponzaLocalBounds;
	if (!ComputeSponzaCourtyardAnchor(sponzaData, mCourtyardAnchor))
	{
		mCourtyardAnchor = {
			(sponzaLocalBounds.Min.x + sponzaLocalBounds.Max.x) * 0.5f,
			sponzaLocalBounds.Min.y,
			(sponzaLocalBounds.Min.z + sponzaLocalBounds.Max.z) * 0.5f
		};
	}
	mRenderer.SetParticleEmitter(mCourtyardAnchor);

	// Масштаб камня под размер Sponza (чтобы не перекрывать целые арки/стены).
	const Aabb rockLocalBounds = ComputeMeshLocalBounds(rockData);
	const float rockExtent = (std::max)(
		(std::max)(rockLocalBounds.Max.x - rockLocalBounds.Min.x, rockLocalBounds.Max.y - rockLocalBounds.Min.y),
		rockLocalBounds.Max.z - rockLocalBounds.Min.z);
	const float sponzaExtent = (std::max)(
		(std::max)(sponzaLocalBounds.Max.x - sponzaLocalBounds.Min.x, sponzaLocalBounds.Max.y - sponzaLocalBounds.Min.y),
		sponzaLocalBounds.Max.z - sponzaLocalBounds.Min.z);
	const float rockTarget = (std::max)(4.0f, sponzaExtent * 0.04f);
	const float rockScale = (rockExtent > 1e-5f) ? (rockTarget / rockExtent) : 1.0f;
	const float instanceSpacing = (std::max)(rockTarget * 1.08f, 10.0f);

	const float courtyardX = mCourtyardAnchor.x;
	const float courtyardY = mCourtyardAnchor.y;
	const float courtyardZ = mCourtyardAnchor.z;

	// Камни на крыше Sponza (низ меша = верх AABB здания + зазор).
	const float rockFloorY = sponzaLocalBounds.Max.y + kRockClearanceAboveSponzaTop;
	mRockClusterCenterY = rockFloorY + rockTarget * 0.55f;

	mMeshLocalBounds = rockLocalBounds;
	BuildSceneInstances(
		rockLocalBounds,
		rockScale,
		rockFloorY,
		{courtyardX, courtyardY, courtyardZ},
		instanceSpacing);

	const Aabb cerberusLocalBounds = ComputeMeshLocalBounds(cerberusData);
	const float cerberusExtent = (std::max)(
		(std::max)(cerberusLocalBounds.Max.x - cerberusLocalBounds.Min.x,
			cerberusLocalBounds.Max.y - cerberusLocalBounds.Min.y),
		cerberusLocalBounds.Max.z - cerberusLocalBounds.Min.z);
	const float cerberusTarget = (std::max)(4.0f, sponzaExtent * 0.04f);
	const float cerberusScale = (cerberusExtent > 1e-5f) ? (cerberusTarget / cerberusExtent) : 1.0f;
	const XMFLOAT3 cerberusAnchor = {
		courtyardX,
		ComputeSponzaSecondFloorY(sponzaLocalBounds),
		courtyardZ};
	const XMMATRIX propWorld =
		ComposeWorldOnFloor(cerberusLocalBounds, cerberusScale, XM_PI * 0.5f, cerberusAnchor);
	XMStoreFloat4x4(&mPropWorld, propWorld);
	mPropWorldBounds = TransformAabb(cerberusLocalBounds, propWorld);
	mSceneWorldBounds.Merge(mPropWorldBounds);

	mShadowSceneBounds = mSponzaWorldBounds;
	mShadowSceneBounds.Merge(mPropWorldBounds);

	FitCameraToScene();
}

void ObjTexturesDemoApp::BuildSceneInstances(
	const Aabb& localBounds,
	float rockScale,
	float floorY,
	const XMFLOAT3& gridCenter,
	float instanceSpacing)
{
	mInstances.clear();
	mInstances.reserve(static_cast<size_t>(kInstanceGridX * kInstanceGridZ));

	for (int iz = 0; iz < kInstanceGridZ; ++iz)
	{
		for (int ix = 0; ix < kInstanceGridX; ++ix)
		{
			const float ox = (static_cast<float>(ix) - (kInstanceGridX - 1) * 0.5f) * instanceSpacing;
			const float oz = (static_cast<float>(iz) - (kInstanceGridZ - 1) * 0.5f) * instanceSpacing;
			const float yaw = static_cast<float>((ix * 17 + iz * 31) % 360) * (XM_PI / 180.f);

			const XMFLOAT3 anchor = {gridCenter.x + ox, floorY, gridCenter.z + oz};
			const XMMATRIX world = ComposeWorldOnFloor(localBounds, rockScale, yaw, anchor);

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
	const float cx = mCourtyardAnchor.x;
	const float cz = mCourtyardAnchor.z;
	const float cameraY = mCourtyardAnchor.y + 8.0f;
	const float dist = 95.0f;

	// Взгляд в центр двора (Cerberus, стены), не вверх на камни — иначе в кадре только небо.
	mCameraPos = {cx, cameraY, cz - dist};
	mYaw = 0.f;
	mPitch = 0.12f;
	mSkipNextMouseLook = true;
}
