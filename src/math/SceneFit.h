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

// Центрирует меше-сцену у начала координат, масштабирует к целевому размеру, задаёт камеру «как было» для орбиты.
SceneFitResult ComputeSceneFit(const ObjMeshData& mesh);
