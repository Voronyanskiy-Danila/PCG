#pragma once

#include <DirectXMath.h>

// Минимум для приложения (камера, константы, материалы).
namespace MathUtils
{

inline constexpr float Pi = 3.14159265f;

template<typename T>
inline T Clamp(const T& x, const T& low, const T& high)
{
	return x < low ? low : (x > high ? high : x);
}

inline DirectX::XMVECTOR SphericalToCartesian(float radius, float theta, float phi)
{
	return DirectX::XMVectorSet(
		radius * sinf(phi) * cosf(theta),
		radius * cosf(phi),
		radius * sinf(phi) * sinf(theta),
		1.0f);
}

inline DirectX::XMMATRIX InverseTranspose(DirectX::CXMMATRIX M)
{
	using namespace DirectX;
	XMMATRIX A = M;
	A.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMVECTOR det = XMMatrixDeterminant(A);
	return XMMatrixTranspose(XMMatrixInverse(&det, A));
}

inline DirectX::XMFLOAT4X4 Identity4x4()
{
	return DirectX::XMFLOAT4X4(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);
}

}
