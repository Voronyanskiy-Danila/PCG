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
#include "../math/SceneFit.h"

#include <unordered_map>
#include <vector>

namespace
{
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
std::unordered_map<std::string, int> ObjTexturesDemoApp::LoadMaterialTextureSets(const ObjMeshData& data)
{
	const UINT slotsPerMaterial = 3u;
	UINT nextSlot = 1u;
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
		nextSlot += slotsPerMaterial;
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
	ObjMeshData data;
	const wchar_t* kObjPath = L"content/models/rock_07/rock_07.obj";

	if (!LoadWavefrontObj(kObjPath, data, err))
	{
		std::wstring msg = err;
		msg += L"\n\nExpected: content/models/rock_07/rock_07.obj (+ .mtl, textures/).";
		msg += L"\nRun: powershell -File tools/download_rock07.ps1";
		throw DxException(E_FAIL, msg, AnsiToWString(__FILE__), __LINE__);
	}

	const UINT slotsPerMaterial = 3u;
	const UINT materialSlots = static_cast<UINT>(data.Materials.size()) * slotsPerMaterial;
	// После материалов в heap идут SRV G-buffer + structured buffer огней (RenderingSystem)
	mDeferredSrvHeapBase = 1u + materialSlots;
	const UINT srvCount = mDeferredSrvHeapBase + GBuffer::kRtCount + 1u;
	BuildDescriptorHeaps(srvCount);
	BuildConstantBuffers();

	mTextureGPU.assign(srvCount, {});
	mTextureUploads.assign(srvCount, {});

	LoadTextureToSrvSlot(0u, L"content/models/white.dds");

	const std::unordered_map<std::string, int> matToSrvBase = LoadMaterialTextureSets(data);

	BuildModelGeometry(data);
	mDrawSubmeshes = BuildDrawSubmeshes(data, matToSrvBase);
	const SceneFitResult fit = ComputeSceneFit(data);
	mWorld = fit.World;
	mCameraPos = fit.CameraPos;
	mYaw = fit.CameraYaw;
	mPitch = fit.CameraPitch;
}
