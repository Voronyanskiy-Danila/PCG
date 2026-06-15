#include "BoundingBox.h"

#include "../importers/Importer_Wavefront_ObjMtl.h"

#include <algorithm>
#include <cmath>
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

bool AabbIntersects(const Aabb& a, const Aabb& b)
{
	if (!a.IsValid() || !b.IsValid())
		return false;
	return a.Min.x <= b.Max.x && a.Max.x >= b.Min.x && a.Min.y <= b.Max.y && a.Max.y >= b.Min.y &&
		a.Min.z <= b.Max.z && a.Max.z >= b.Min.z;
}

Aabb ComputeMeshLocalBounds(const ObjMeshData& mesh)
{
	Aabb b{};
	if (mesh.Positions.empty())
		return b;

	b.Min = mesh.Positions[0];
	b.Max = mesh.Positions[0];
	for (const XMFLOAT3& p : mesh.Positions)
		b.ExpandPoint(p);
	return b;
}

Aabb ComputeSubmeshLocalBounds(const ObjMeshData& mesh, const ObjSubmeshRange& submesh)
{
	Aabb b{};
	if (mesh.Positions.empty() || submesh.IndexCount == 0)
		return b;

	bool havePoint = false;
	for (uint32_t i = 0; i < submesh.IndexCount; ++i)
	{
		const uint32_t vi = mesh.Indices32[submesh.StartIndexLocation + i];
		if (vi >= mesh.Positions.size())
			continue;
		if (!havePoint)
		{
			b.Min = mesh.Positions[vi];
			b.Max = mesh.Positions[vi];
			havePoint = true;
		}
		else
		{
			b.ExpandPoint(mesh.Positions[vi]);
		}
	}
	return b;
}

bool ComputeSponzaCourtyardAnchor(const ObjMeshData& mesh, XMFLOAT3& outAnchor, float* outFloorTopY)
{
	const Aabb full = ComputeMeshLocalBounds(mesh);
	if (!full.IsValid() || mesh.Submeshes.empty())
		return false;

	auto setResult = [&](const Aabb& b) {
		outAnchor = {
			(b.Min.x + b.Max.x) * 0.5f,
			(b.Min.y + b.Max.y) * 0.5f,
			(b.Min.z + b.Max.z) * 0.5f
		};
		if (outFloorTopY)
			*outFloorTopY = b.Max.y;
	};

	// Двор Sponza: материал "floor" встречается дважды — берём самый нижний (1-й этаж, не галерея).
	bool foundFloorMaterial = false;
	float lowestFloorMinY = std::numeric_limits<float>::max();
	Aabb groundFloorBounds{};

	for (const ObjSubmeshRange& sm : mesh.Submeshes)
	{
		if (sm.MaterialName != "floor")
			continue;

		const Aabb b = ComputeSubmeshLocalBounds(mesh, sm);
		if (!b.IsValid())
			continue;

		if (b.Min.y < lowestFloorMinY)
		{
			lowestFloorMinY = b.Min.y;
			groundFloorBounds = b;
			foundFloorMaterial = true;
		}
	}

	if (foundFloorMaterial)
	{
		setResult(groundFloorBounds);
		return true;
	}

	// Запасной вариант: самая большая тонкая горизонтальная плита у основания здания.
	const float spanY = full.Max.y - full.Min.y;
	const float groundBandTop = full.Min.y + spanY * 0.22f;

	float bestArea = -1.0f;
	Aabb bestBounds{};

	for (const ObjSubmeshRange& sm : mesh.Submeshes)
	{
		const Aabb b = ComputeSubmeshLocalBounds(mesh, sm);
		if (!b.IsValid())
			continue;

		const float height = b.Max.y - b.Min.y;
		if (height > 1.0f)
			continue;

		const float centerY = (b.Min.y + b.Max.y) * 0.5f;
		if (centerY > groundBandTop)
			continue;

		const float areaXZ = (b.Max.x - b.Min.x) * (b.Max.z - b.Min.z);
		if (areaXZ < 100.0f)
			continue;

		if (areaXZ <= bestArea)
			continue;

		bestArea = areaXZ;
		bestBounds = b;
	}

	if (bestArea < 0.0f)
	{
		const float fallbackY = full.Min.y + spanY * 0.12f;
		outAnchor = {
			(full.Min.x + full.Max.x) * 0.5f,
			fallbackY,
			(full.Min.z + full.Max.z) * 0.5f
		};
		if (outFloorTopY)
			*outFloorTopY = fallbackY;
		return false;
	}

	setResult(bestBounds);
	return true;
}

float ComputeSponzaSecondFloorY(const Aabb& sponzaLocalBounds)
{
	if (!sponzaLocalBounds.IsValid())
		return 0.f;

	const float spanY = sponzaLocalBounds.Max.y - sponzaLocalBounds.Min.y;
	return sponzaLocalBounds.Min.y + spanY * 0.46f;
}

float ComputeSponzaThirdFloorY(const Aabb& sponzaLocalBounds)
{
	if (!sponzaLocalBounds.IsValid())
		return 0.f;

	const float spanY = sponzaLocalBounds.Max.y - sponzaLocalBounds.Min.y;
	return sponzaLocalBounds.Min.y + spanY * 0.57f;
}

XMMATRIX ComposeWorldOnFloor(const Aabb& localBounds, float uniformScale, float rotationY, XMFLOAT3 anchorOnFloor)
{
	if (!localBounds.IsValid())
		return XMMatrixIdentity();

	const XMFLOAT3 pivot = {
		(localBounds.Min.x + localBounds.Max.x) * 0.5f,
		localBounds.Min.y,
		(localBounds.Min.z + localBounds.Max.z) * 0.5f
	};

	const XMMATRIX scaleRot =
		XMMatrixTranslation(-pivot.x, -pivot.y, -pivot.z) *
		XMMatrixScaling(uniformScale, uniformScale, uniformScale) *
		XMMatrixRotationY(rotationY);

	const Aabb scaled = TransformAabb(localBounds, scaleRot);
	const float liftY = anchorOnFloor.y - scaled.Min.y;

	return scaleRot * XMMatrixTranslation(anchorOnFloor.x, liftY, anchorOnFloor.z);
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
