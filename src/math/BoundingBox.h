#pragma once

#include <DirectXMath.h>
#include <cstdint>

struct Aabb
{
	DirectX::XMFLOAT3 Min = {0.f, 0.f, 0.f};
	DirectX::XMFLOAT3 Max = {0.f, 0.f, 0.f};

	void ExpandPoint(const DirectX::XMFLOAT3& p);
	void Merge(const Aabb& other);
	bool IsValid() const;
};

bool AabbIntersects(const Aabb& a, const Aabb& b);

Aabb ComputeMeshLocalBounds(const struct ObjMeshData& mesh);

Aabb TransformAabb(const Aabb& local, DirectX::CXMMATRIX world);
