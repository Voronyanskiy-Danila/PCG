// =============================================================================
// SceneFit.h — автоматическая расстановка камеры и масштаба модели (Lab 3)
// =============================================================================
//
// После загрузки rock_07.obj: центрирование, uniform scale до ~10 единиц,
// стартовая позиция камера на сфере вокруг объекта.
// =============================================================================

#pragma once

#include "../importers/Importer_Wavefront_ObjMtl.h"
#include <DirectXMath.h>

struct SceneFitResult
{
	DirectX::XMFLOAT4X4 World = {};
	DirectX::XMFLOAT3 CameraPos = {};
	float CameraYaw = 0.f;
	float CameraPitch = 0.f;
};

SceneFitResult ComputeSceneFit(const ObjMeshData& mesh);
