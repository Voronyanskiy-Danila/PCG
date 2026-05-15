#include "ObjTexturesDemoApp.h"

#include "../importers/Importer_Image_DirectXTex.h"
#include "../importers/Importer_Wavefront_ObjMtl.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	struct DiffuseSlotPlan
	{
		std::unordered_map<std::wstring, int> pathToSrvSlot;
		std::vector<std::wstring> loadOrder;
	};

	DiffuseSlotPlan BuildDiffuseSlotPlan(const ObjMeshData& data)
	{
		DiffuseSlotPlan plan;
		std::unordered_set<std::string> usedMat;
		usedMat.reserve(data.Submeshes.size());
		for (const auto& sm : data.Submeshes)
			usedMat.insert(sm.MaterialName);

		for (const auto& matName : usedMat)
		{
			auto itMat = data.Materials.find(matName);
			if (itMat == data.Materials.end())
				continue;
			const std::wstring& path = itMat->second.DiffuseTexturePath;
			if (path.empty() || plan.pathToSrvSlot.find(path) != plan.pathToSrvSlot.end())
				continue;
			plan.pathToSrvSlot[path] = static_cast<int>(1 + static_cast<int>(plan.loadOrder.size()));
			plan.loadOrder.push_back(path);
		}
		return plan;
	}

	std::vector<DrawSubmesh> BuildDrawSubmeshes(
		const ObjMeshData& data,
		const std::unordered_map<std::wstring, int>& pathToSrv)
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
				d.DiffuseSrvIndex = 0;
				d.HasDiffuseTexture = false;
				out.push_back(d);
				continue;
			}
			const MtlMaterial& m = itMat->second;
			d.Ka = m.Ka;
			d.Kd = m.Kd;
			d.Ks = m.Ks;
			d.Ns = m.Ns;
			const bool hasTex = !m.DiffuseTexturePath.empty();
			d.HasDiffuseTexture = hasTex;
			if (!hasTex)
				d.DiffuseSrvIndex = 0;
			else
			{
				auto tp = pathToSrv.find(m.DiffuseTexturePath);
				d.DiffuseSrvIndex = (tp != pathToSrv.end()) ? tp->second : 0;
			}
			out.push_back(d);
		}
		return out;
	}
}

void ObjTexturesDemoApp::LoadModelAndTextures()
{
	std::wstring err;
	ObjMeshData data;
	const wchar_t* kObjPath = L"content/models/sponza/sponza.obj";

	if (!LoadWavefrontObj(kObjPath, data, err))
	{
		std::wstring msg = err;
		msg += L"\n\nExpected: content/models/sponza/sponza.obj, sponza.mtl, textures/*.tga";
		throw DxException(E_FAIL, msg, AnsiToWString(__FILE__), __LINE__);
	}

	const DiffuseSlotPlan plan = BuildDiffuseSlotPlan(data);

	mDeferredSrvHeapBase = 1u + static_cast<UINT>(plan.loadOrder.size());
	const UINT srvCount = mDeferredSrvHeapBase + GBuffer::kRtCount + 1u;
	BuildDescriptorHeaps(srvCount);
	BuildConstantBuffers();

	mTextureGPU.assign(srvCount, {});
	mTextureUploads.assign(srvCount, {});

	HRESULT hr = LoadTextureImageFromFile12(
		md3dDevice.Get(),
		mCommandList.Get(),
		L"content/models/white.dds",
		mTextureGPU[0],
		mTextureUploads[0]);
	ThrowIfFailed(hr);
	CreateSrvForTexture(0, mTextureGPU[0].Get());

	for (size_t i = 0; i < plan.loadOrder.size(); ++i)
	{
		const UINT slot = static_cast<UINT>(1u + i);
		hr = LoadTextureImageFromFile12(
			md3dDevice.Get(),
			mCommandList.Get(),
			plan.loadOrder[i].c_str(),
			mTextureGPU[slot],
			mTextureUploads[slot]);
		if (FAILED(hr))
		{
			hr = LoadTextureImageFromFile12(
				md3dDevice.Get(),
				mCommandList.Get(),
				L"content/models/white.dds",
				mTextureGPU[slot],
				mTextureUploads[slot]);
		}
		ThrowIfFailed(hr);
		CreateSrvForTexture(static_cast<int>(slot), mTextureGPU[slot].Get());
	}

	BuildModelGeometry(data);
	mDrawSubmeshes = BuildDrawSubmeshes(data, plan.pathToSrvSlot);
	FitWorldAndCameraToMesh(data);
}
