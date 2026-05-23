// =============================================================================
// SceneFit.cpp — подгонка сцены под камень Rock 07
// =============================================================================
//
// Без этого камень мог бы быть в километрах от камеры или слишком мелким;
// TessNear/TessFar в ObjectConstants рассчитаны на масштаб после scale ~10.
// =============================================================================

#include "SceneFit.h"

#include "MathUtils.h"

#include <algorithm>

using namespace DirectX;

SceneFitResult ComputeSceneFit(const ObjMeshData& mesh)
{
	SceneFitResult r{};
	if (mesh.Positions.empty())
		return r;

	// AABB в локальных координатах OBJ
	XMFLOAT3 mn = mesh.Positions[0];
	XMFLOAT3 mx = mesh.Positions[0];
	for (const XMFLOAT3& p : mesh.Positions)
	{
		mn.x = (std::min)(mn.x, p.x);
		mn.y = (std::min)(mn.y, p.y);
		mn.z = (std::min)(mn.z, p.z);
		mx.x = (std::max)(mx.x, p.x);
		mx.y = (std::max)(mx.y, p.y);
		mx.z = (std::max)(mx.z, p.z);
	}

	const XMVECTOR mnV = XMLoadFloat3(&mn);
	const XMVECTOR mxV = XMLoadFloat3(&mx);
	const XMVECTOR center = XMVectorScale(XMVectorAdd(mnV, mxV), 0.5f);
	XMFLOAT3 centerF;
	XMStoreFloat3(&centerF, center);

	const XMVECTOR ext = XMVectorSubtract(mxV, mnV);
	const float ex = (std::max)({XMVectorGetX(ext), XMVectorGetY(ext), XMVectorGetZ(ext)});
	constexpr float kTargetExtents = 10.0f;
	const float scale = (ex > 1e-6f) ? (kTargetExtents / ex) : 1.0f;

	// World = сдвиг в origin + равномерный масштаб
	const XMMATRIX world =
		XMMatrixTranslation(-centerF.x, -centerF.y, -centerF.z) * XMMatrixScaling(scale, scale, scale);
	XMStoreFloat4x4(&r.World, world);

	const float diagonal = XMVectorGetX(XMVector3Length(ext));
	const float sceneSize = diagonal * scale;
	const float dist = (std::min)(200.0f, (std::max)(14.0f, sceneSize * 0.62f));

	const float theta = 1.5f * XM_PI;
	const float phi = XM_PIDIV4;
	const XMVECTOR pos = MathUtils::SphericalToCartesian(dist, theta, phi);
	XMStoreFloat3(&r.CameraPos, pos);
	const XMVECTOR toCenter = XMVector3Normalize(XMVectorNegate(pos));
	r.CameraYaw = atan2f(XMVectorGetX(toCenter), XMVectorGetZ(toCenter));
	r.CameraPitch = asinf(MathUtils::Clamp(XMVectorGetY(toCenter), -0.99f, 0.99f));

	return r;
}
