#pragma once

#include <DirectXMath.h>
#include <cstdint>

struct ParticleGpu
{
	DirectX::XMFLOAT3 Position = {0.0f, 0.0f, 0.0f};
	float Life = 0.0f;

	DirectX::XMFLOAT3 Velocity = {0.0f, 0.0f, 0.0f};
	float Size = 0.25f;

	DirectX::XMFLOAT4 Color = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct ParticleSimConstants
{
	float DeltaTime = 0.0f;
	float Gravity = -9.8f;
	uint32_t MaxParticles = 0u;
	uint32_t SpawnCount = 0u;
	DirectX::XMFLOAT3 EmitterPos = {0.0f, 0.5f, 0.0f};
	float _Pad0 = 0.0f;
};

struct ParticleDrawConstants
{
	DirectX::XMFLOAT4X4 ViewProj = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	DirectX::XMFLOAT3 CameraRight = {1.0f, 0.0f, 0.0f};
	float _Pad0 = 0.0f;
	DirectX::XMFLOAT3 CameraUp = {0.0f, 1.0f, 0.0f};
	float _Pad1 = 0.0f;
};
