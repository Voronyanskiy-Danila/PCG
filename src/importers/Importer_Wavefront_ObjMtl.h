// =============================================================================
// Importer_Wavefront_ObjMtl.h — загрузка OBJ/MTL для Lab 3/8
// =============================================================================
//
// MtlMaterial — карты для deferred_tessellation.hlsl:
//   DiffuseTexturePath      — map_Kd  → t0 (albedo, sRGB→linear в PS)
//   NormalTexturePath       — map_Bump / norm → t1
//   DisplacementTexturePath — map_disp → t2
//   RmTexturePath           — map_ARM / map_roughness → t3 (R=AO, G=rough, B=metal)
//
// Pr / Pm — множители roughness / metallic (Lab 8 PBR).
// Ks / Ns — legacy Phong; Ns используется как fallback roughness без ARM.
// =============================================================================

#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct MtlMaterial
{
	std::string Name;
	DirectX::XMFLOAT3 Ka = {0.2f, 0.2f, 0.2f};
	DirectX::XMFLOAT3 Kd = {0.8f, 0.8f, 0.8f};
	DirectX::XMFLOAT3 Ks = {0.2f, 0.2f, 0.2f};
	float Ns = 32.0f;
	std::wstring DiffuseTexturePath;
	std::wstring NormalTexturePath;
	std::wstring DisplacementTexturePath;
	std::wstring RmTexturePath;
	float RoughnessFactor = 1.0f;
	float MetallicFactor = 0.0f;
	bool HasMetallicFactor = false;
	// Pipeline hints (from MTL Pc* tags or ApplyMaterialPipelineHints after load)
	DirectX::XMFLOAT2 UvScale = {1.0f, 1.0f};
	bool NormalFlipY = false;
	bool SkipNormalMap = false;
};

struct ObjSubmeshRange
{
	std::string MaterialName;
	uint32_t StartIndexLocation = 0;
	uint32_t IndexCount = 0;
};

struct ObjMeshData
{
	std::vector<DirectX::XMFLOAT3> Positions;
	std::vector<DirectX::XMFLOAT3> Normals;
	std::vector<DirectX::XMFLOAT2> Texcoords;
	std::vector<std::uint32_t> Indices32;
	std::vector<ObjSubmeshRange> Submeshes;
	std::unordered_map<std::string, MtlMaterial> Materials;
};

bool LoadWavefrontObj(const std::wstring& objPath, ObjMeshData& outMesh, std::wstring& errorMessage);
