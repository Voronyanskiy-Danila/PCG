#include "Frustum.h"

using namespace DirectX;

namespace
{

void NormalizePlane(XMFLOAT4& p)
{
	XMVECTOR plane = XMLoadFloat4(&p);
	const float len = XMVectorGetX(XMVector3Length(plane));
	if (len > 1e-6f)
		plane = XMVectorScale(plane, 1.f / len);
	XMStoreFloat4(&p, plane);
}

} // namespace

void Frustum::ExtractPlanes(CXMMATRIX m, XMFLOAT4 out[6])
{
	XMFLOAT4X4 mat;
	XMStoreFloat4x4(&mat, m);

	out[0] = {mat._14 + mat._11, mat._24 + mat._21, mat._34 + mat._31, mat._44 + mat._41};
	out[1] = {mat._14 - mat._11, mat._24 - mat._21, mat._34 - mat._31, mat._44 - mat._41};
	out[2] = {mat._14 + mat._12, mat._24 + mat._22, mat._34 + mat._32, mat._44 + mat._42};
	out[3] = {mat._14 - mat._12, mat._24 - mat._22, mat._34 - mat._32, mat._44 - mat._42};
	out[4] = {mat._13 - mat._14, mat._23 - mat._24, mat._33 - mat._34, mat._43 - mat._44};
	out[5] = {mat._14 - mat._13, mat._24 - mat._23, mat._34 - mat._33, mat._44 - mat._43};

	for (int i = 0; i < 6; ++i)
		NormalizePlane(out[i]);
}

void Frustum::ExtractFromMatrix(CXMMATRIX viewProj)
{
	XMStoreFloat4x4(&ViewProj, viewProj);
	ExtractPlanes(viewProj, Planes);
}

bool Frustum::IntersectsAabbPlanes(const Aabb& box, const XMFLOAT4 planes[6]) const
{
	for (int i = 0; i < 6; ++i)
	{
		const XMFLOAT4& plane = planes[i];
		const XMVECTOR n = XMLoadFloat4(&plane);

		const XMVECTOR pCorner = XMVectorSet(
			plane.x >= 0.f ? box.Max.x : box.Min.x,
			plane.y >= 0.f ? box.Max.y : box.Min.y,
			plane.z >= 0.f ? box.Max.z : box.Min.z,
			1.f);
		const XMVECTOR nCorner = XMVectorSet(
			plane.x >= 0.f ? box.Min.x : box.Max.x,
			plane.y >= 0.f ? box.Min.y : box.Max.y,
			plane.z >= 0.f ? box.Min.z : box.Max.z,
			1.f);

		if (XMVectorGetX(XMPlaneDotCoord(n, pCorner)) < 0.f &&
			XMVectorGetX(XMPlaneDotCoord(n, nCorner)) < 0.f)
			return false;
	}
	return true;
}

bool Frustum::IntersectsAabbClip(const Aabb& box, CXMMATRIX clipRowMatrix, const XMFLOAT4 planes[6]) const
{
	(void)planes;

	const XMFLOAT3 corners[8] = {
		{box.Min.x, box.Min.y, box.Min.z},
		{box.Max.x, box.Min.y, box.Min.z},
		{box.Min.x, box.Max.y, box.Min.z},
		{box.Max.x, box.Max.y, box.Min.z},
		{box.Min.x, box.Min.y, box.Max.z},
		{box.Max.x, box.Min.y, box.Max.z},
		{box.Min.x, box.Max.y, box.Max.z},
		{box.Max.x, box.Max.y, box.Max.z},
	};

	XMVECTOR clip[8];
	for (int i = 0; i < 8; ++i)
		clip[i] = XMVector4Transform(XMVectorSet(corners[i].x, corners[i].y, corners[i].z, 1.f), clipRowMatrix);

	// AABB vs frustum in homogeneous clip space (D3D LH: -w<=x<=w, -w<=y<=w, 0<=z<=w).
	auto allOutside = [&](auto&& outside) {
		for (int i = 0; i < 8; ++i)
		{
			if (!outside(clip[i]))
				return false;
		}
		return true;
	};

	if (allOutside([](XMVECTOR c) { return XMVectorGetX(c) + XMVectorGetW(c) < 0.f; }))
		return false;
	if (allOutside([](XMVECTOR c) { return XMVectorGetX(c) - XMVectorGetW(c) > 0.f; }))
		return false;
	if (allOutside([](XMVECTOR c) { return XMVectorGetY(c) + XMVectorGetW(c) < 0.f; }))
		return false;
	if (allOutside([](XMVECTOR c) { return XMVectorGetY(c) - XMVectorGetW(c) > 0.f; }))
		return false;
	if (allOutside([](XMVECTOR c) { return XMVectorGetZ(c) < 0.f; }))
		return false;
	if (allOutside([](XMVECTOR c) { return XMVectorGetZ(c) - XMVectorGetW(c) > 0.f; }))
		return false;

	return true;
}

bool Frustum::IntersectsAabb(const Aabb& box) const
{
	if (!box.IsValid())
		return false;

	// Row-vector clip: corner * ViewProj (DirectXMath XMVector4Transform).
	return IntersectsAabbClip(box, XMLoadFloat4x4(&ViewProj), Planes);
}

bool Frustum::IntersectsAabb(const Aabb& box, CXMMATRIX clipTransform) const
{
	if (!box.IsValid())
		return false;

	return IntersectsAabbClip(box, clipTransform, Planes);
}
