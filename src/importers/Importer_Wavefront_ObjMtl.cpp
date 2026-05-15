#include "Importer_Wavefront_ObjMtl.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

using namespace DirectX;

namespace
{
std::string Trim(std::string s)
{
	auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
	s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
	return s;
}

std::vector<std::string> SplitWhitespace(const std::string& line)
{
	std::istringstream iss(line);
	std::vector<std::string> out;
	std::string tok;
	while (iss >> tok)
		out.push_back(tok);
	return out;
}

bool ParseMtlFile(const fs::path& mtlPath, std::unordered_map<std::string, MtlMaterial>& materials, std::wstring& err)
{
	std::ifstream in(mtlPath);
	if (!in)
	{
		err = L"Cannot open MTL: " + mtlPath.wstring();
		return false;
	}

	std::string activeMtlName;
	std::string line;
	while (std::getline(in, line))
	{
		line = Trim(line);
		if (line.empty() || line[0] == '#')
			continue;

		auto parts = SplitWhitespace(line);
		if (parts.empty())
			continue;

		const std::string& tag = parts[0];
		if (tag == "newmtl")
		{
			if (parts.size() < 2)
				continue;
			MtlMaterial m;
			m.Name = parts[1];
			materials[m.Name] = m;
			activeMtlName = m.Name;
		}
		else if (tag == "Ka" && !activeMtlName.empty() && parts.size() >= 4)
		{
			materials[activeMtlName].Ka = XMFLOAT3(std::stof(parts[1]), std::stof(parts[2]), std::stof(parts[3]));
		}
		else if (tag == "Kd" && !activeMtlName.empty() && parts.size() >= 4)
		{
			materials[activeMtlName].Kd = XMFLOAT3(std::stof(parts[1]), std::stof(parts[2]), std::stof(parts[3]));
		}
		else if (tag == "Ks" && !activeMtlName.empty() && parts.size() >= 4)
		{
			materials[activeMtlName].Ks = XMFLOAT3(std::stof(parts[1]), std::stof(parts[2]), std::stof(parts[3]));
		}
		else if (tag == "Ns" && !activeMtlName.empty() && parts.size() >= 2)
		{
			materials[activeMtlName].Ns = std::stof(parts[1]);
		}
		else if ((tag == "map_Kd" || tag == "map_kd") && !activeMtlName.empty() && parts.size() >= 2)
		{
			fs::path texRel = fs::path(Trim(parts[1])).lexically_normal();
			if (!texRel.empty())
				materials[activeMtlName].DiffuseTexturePath = (mtlPath.parent_path() / texRel).wstring();
		}
	}

	return true;
}

bool ParseFaceCorner(
	const std::string& corner,
	int& vi,
	int& vti,
	int& vni)
{
	vi = vti = vni = 0;
	std::vector<std::string> comps;
	std::stringstream ss(corner);
	std::string item;
	while (std::getline(ss, item, '/'))
		comps.push_back(item);

	if (comps.empty())
		return false;

	if (!comps[0].empty())
		vi = std::stoi(comps[0]);

	if (comps.size() >= 2)
	{
		if (!comps[1].empty())
			vti = std::stoi(comps[1]);
	}
	if (comps.size() >= 3)
	{
		if (!comps[2].empty())
			vni = std::stoi(comps[2]);
	}
	return true;
}
} // namespace

bool LoadWavefrontObj(const std::wstring& objPath, ObjMeshData& outMesh, std::wstring& errorMessage)
{
	outMesh = {};

	fs::path objP(objPath);
	std::ifstream in(objP);
	if (!in)
	{
		errorMessage = L"Cannot open OBJ: " + objPath;
		return false;
	}

	std::vector<XMFLOAT3> positions;
	std::vector<XMFLOAT3> normals;
	std::vector<XMFLOAT2> texcoords;

	positions.push_back({0, 0, 0});
	normals.push_back({0, 0, 1});
	texcoords.push_back({0, 0});

	std::string activeMaterial;
	uint32_t submeshIndexStart = 0;
	bool haveFacesInCurrent = false;

	std::string line;
	while (std::getline(in, line))
	{
		line = Trim(line);
		if (line.empty() || line[0] == '#')
			continue;

		auto parts = SplitWhitespace(line);
		if (parts.empty())
			continue;

		const std::string& tag = parts[0];
		if (tag == "mtllib")
		{
			if (parts.size() < 2)
				continue;
			fs::path mtlFile = objP.parent_path() / parts[1];
			if (!ParseMtlFile(mtlFile, outMesh.Materials, errorMessage))
				return false;
		}
		else if (tag == "v" && parts.size() >= 4)
		{
			positions.push_back(XMFLOAT3(
				std::stof(parts[1]),
				std::stof(parts[2]),
				std::stof(parts[3])));
		}
		else if (tag == "vn" && parts.size() >= 4)
		{
			normals.push_back(XMFLOAT3(
				std::stof(parts[1]),
				std::stof(parts[2]),
				std::stof(parts[3])));
		}
		else if (tag == "vt" && parts.size() >= 3)
		{
			// Координаты как в файле; коррекцию под D3D/развёртку делаем в шейдере (одно место).
			texcoords.push_back(XMFLOAT2(std::stof(parts[1]), std::stof(parts[2])));
		}
		else if (tag == "usemtl")
		{
			if (parts.size() < 2)
				continue;
			if (haveFacesInCurrent && outMesh.Indices32.size() > submeshIndexStart)
			{
				ObjSubmeshRange sm;
				sm.MaterialName = activeMaterial;
				sm.StartIndexLocation = submeshIndexStart;
				sm.IndexCount = static_cast<uint32_t>(outMesh.Indices32.size() - submeshIndexStart);
				outMesh.Submeshes.push_back(sm);
				submeshIndexStart = static_cast<uint32_t>(outMesh.Indices32.size());
				haveFacesInCurrent = false;
			}
			activeMaterial = parts[1];
		}
		else if (tag == "f")
		{
			if (parts.size() < 4)
				continue;

			struct FaceCorner
			{
				int vi;
				int vti;
				int vni;
			};
			std::vector<FaceCorner> faceCorners;
			for (size_t i = 1; i < parts.size(); ++i)
			{
				int vi = 0, vti = 0, vni = 0;
				if (!ParseFaceCorner(parts[i], vi, vti, vni))
					continue;
				faceCorners.push_back(FaceCorner{vi, vti, vni});
			}
			if (faceCorners.size() < 3)
				continue;

			auto emitCorner = [&](const FaceCorner& c) {
				const int pi = (c.vi >= 0) ? c.vi : static_cast<int>(positions.size()) + c.vi;
				const int ti = (c.vti >= 0) ? c.vti : static_cast<int>(texcoords.size()) + c.vti;
				const int ni = (c.vni >= 0) ? c.vni : static_cast<int>(normals.size()) + c.vni;

				XMFLOAT3 p = (pi > 0 && pi < static_cast<int>(positions.size())) ? positions[static_cast<size_t>(pi)] : XMFLOAT3(0, 0, 0);
				XMFLOAT3 n = (ni > 0 && ni < static_cast<int>(normals.size())) ? normals[static_cast<size_t>(ni)] : XMFLOAT3(0, 0, 1);
				XMFLOAT2 t = (ti > 0 && ti < static_cast<int>(texcoords.size())) ? texcoords[static_cast<size_t>(ti)] : XMFLOAT2(0, 0);

				outMesh.Positions.push_back(p);
				outMesh.Normals.push_back(n);
				outMesh.Texcoords.push_back(t);
				outMesh.Indices32.push_back(static_cast<uint32_t>(outMesh.Positions.size() - 1));
			};

			for (size_t i = 1; i + 1 < faceCorners.size(); ++i)
			{
				emitCorner(faceCorners[0]);
				emitCorner(faceCorners[i]);
				emitCorner(faceCorners[i + 1]);
			}
			haveFacesInCurrent = true;
		}
	}

	if (haveFacesInCurrent && outMesh.Indices32.size() > submeshIndexStart)
	{
		ObjSubmeshRange sm;
		sm.MaterialName = activeMaterial;
		sm.StartIndexLocation = submeshIndexStart;
		sm.IndexCount = static_cast<uint32_t>(outMesh.Indices32.size() - submeshIndexStart);
		outMesh.Submeshes.push_back(sm);
	}

	if (outMesh.Positions.empty() || outMesh.Indices32.empty())
	{
		errorMessage = L"OBJ has no geometry.";
		return false;
	}

	return true;
}
