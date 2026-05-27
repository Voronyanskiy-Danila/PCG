#pragma once

#include "BoundingBox.h"
#include <DirectXMath.h>

struct Frustum
{
	DirectX::XMFLOAT4X4 ViewProj{};
	DirectX::XMFLOAT4 Planes[6] = {};

	void ExtractFromMatrix(DirectX::CXMMATRIX viewProj);
	bool IntersectsAabb(const Aabb& box) const;
	bool IntersectsAabb(const Aabb& box, DirectX::CXMMATRIX clipRowMatrix) const;

private:
	static void ExtractPlanes(DirectX::CXMMATRIX m, DirectX::XMFLOAT4 out[6]);
	bool IntersectsAabbPlanes(const Aabb& box, const DirectX::XMFLOAT4 planes[6]) const;
	bool IntersectsAabbClip(const Aabb& box, DirectX::CXMMATRIX clipRowMatrix,
		const DirectX::XMFLOAT4 planes[6]) const;
};
