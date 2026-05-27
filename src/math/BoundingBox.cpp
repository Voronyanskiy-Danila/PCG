#include "BoundingBox.h"

#include "../importers/Importer_Wavefront_ObjMtl.h"

#include <algorithm>
#include <limits>

using namespace DirectX;

void Aabb::ExpandPoint(const XMFLOAT3& p)
{
	Min.x = (std::min)(Min.x, p.x);
	Min.y = (std::min)(Min.y, p.y);
	Min.z = (std::min)(Min.z, p.z);
	Max.x = (std::max)(Max.x, p.x);
	Max.y = (std::max)(Max.y, p.y);
	Max.z = (std::max)(Max.z, p.z);
}

void Aabb::Merge(const Aabb& other)
{
	if (!other.IsValid())
		return;
	if (!IsValid())
	{
		*this = other;
		return;
	}
	ExpandPoint(other.Min);
	ExpandPoint(other.Max);
}

bool Aabb::IsValid() const
{
	return Min.x <= Max.x && Min.y <= Max.y && Min.z <= Max.z;
}

Aabb ComputeMeshLocalBounds(const ObjMeshData& mesh)
{
	Aabb b{};
	if (mesh.Positions.empty())
		return b;

	b.Min = mesh.Positions[0];
	b.Max = mesh.Positions[0];
	for (const XMFLOAT3& p : mesh.Positions)
	{
		b.ExpandPoint(p);
	}
	return b;
}

Aabb TransformAabb(const Aabb& local, CXMMATRIX world)
{
	if (!local.IsValid())
		return {};

	const XMFLOAT3 corners[8] = {
		{local.Min.x, local.Min.y, local.Min.z},
		{local.Max.x, local.Min.y, local.Min.z},
		{local.Min.x, local.Max.y, local.Min.z},
		{local.Max.x, local.Max.y, local.Min.z},
		{local.Min.x, local.Min.y, local.Max.z},
		{local.Max.x, local.Min.y, local.Max.z},
		{local.Min.x, local.Max.y, local.Max.z},
		{local.Max.x, local.Max.y, local.Max.z},
	};

	Aabb out{};
	out.Min = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
		std::numeric_limits<float>::max()};
	out.Max = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
		std::numeric_limits<float>::lowest()};

	for (const XMFLOAT3& c : corners)
	{
		XMFLOAT3 p;
		XMStoreFloat3(&p, XMVector3TransformCoord(XMLoadFloat3(&c), world));
		out.ExpandPoint(p);
	}
	return out;
}
