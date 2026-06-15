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

// Евклидово расстояние от точки до ближайшей точки на AABB (0, если точка внутри).
float DistancePointToAabb(const DirectX::XMFLOAT3& point, const Aabb& box);

Aabb ComputeMeshLocalBounds(const struct ObjMeshData& mesh);

Aabb ComputeSubmeshLocalBounds(const struct ObjMeshData& mesh, const struct ObjSubmeshRange& submesh);

// Центр плоского «пола» двора Sponza (для Cerberus / камней), не AABB всей модели.
bool ComputeSponzaCourtyardAnchor(
	const struct ObjMeshData& mesh,
	DirectX::XMFLOAT3& outAnchor,
	float* outFloorTopY = nullptr);

// Y галереи 2-го этажа Sponza (локальные коорд. OBJ; двор ≈ 35%, 2-й этаж ≈ 46%).
float ComputeSponzaSecondFloorY(const Aabb& sponzaLocalBounds);

// Y галереи 3-го этажа Sponza (≈ 57% высоты модели).
float ComputeSponzaThirdFloorY(const Aabb& sponzaLocalBounds);

// Масштаб + поворот вокруг pivot (центр XZ, Min.y), затем сдвиг так, чтобы Min.y = anchor.y.
DirectX::XMMATRIX ComposeWorldOnFloor(
	const Aabb& localBounds,
	float uniformScale,
	float rotationY,
	DirectX::XMFLOAT3 anchorOnFloor);

Aabb TransformAabb(const Aabb& local, DirectX::CXMMATRIX world);
