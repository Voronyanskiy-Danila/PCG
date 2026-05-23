// =============================================================================
// Importer_Wavefront_ObjMtl.h — загрузка OBJ/MTL для Lab 3
// =============================================================================
//
// MtlMaterial хранит пути к трём картам, нужным шейдеру tessellation:
//   DiffuseTexturePath      — map_Kd  → gDiffuseMap (t0)
//   NormalTexturePath       — map_Bump / norm → gNormalMap (t1)
//   DisplacementTexturePath — map_disp → gDispMap (t2), сдвиг в DomainDS
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
