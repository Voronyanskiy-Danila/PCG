#pragma once

#include "../math/BoundingBox.h"
#include "../math/Frustum.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct OctreeItem
{
	uint32_t Index = 0;
	Aabb Bounds{};
};

class Octree
{
public:
	void Build(const std::vector<OctreeItem>& items);
	void Clear();

	void QueryFrustum(
		const Frustum& frustum,
		const std::vector<OctreeItem>& items,
		const Aabb& localMeshBounds,
		DirectX::CXMMATRIX view,
		DirectX::CXMMATRIX proj,
		const void* instances,
		size_t instanceStrideBytes,
		size_t worldMatrixOffsetBytes,
		uint32_t instanceCount,
		std::vector<uint32_t>& outVisible) const;

	uint32_t GetNodeCount() const { return mNodeCount; }

private:
	struct Node
	{
		Aabb Bounds{};
		std::vector<uint32_t> ObjectIndices;
		std::array<std::unique_ptr<Node>, 8> Children{};

		bool IsLeaf() const
		{
			for (const auto& c : Children)
			{
				if (c)
					return false;
			}
			return true;
		}
	};

	std::unique_ptr<Node> mRoot;
	uint32_t mNodeCount = 0;

	void Subdivide(Node& node, const std::vector<OctreeItem>& items, int depth);
};
