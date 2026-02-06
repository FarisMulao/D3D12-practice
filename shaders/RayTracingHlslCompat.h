#ifndef RAYTRACINGHLSLCOMPAT_H
#define RAYTRACINGHLSLCOMPAT_H

#ifdef HLSL
// HLSL side
typedef float4x4 XMMATRIX;
typedef float4 XMFLOAT4;
typedef float3 XMFLOAT3;
#else
// C++ side
#include <DirectXMath.h>
#define float4x4 DirectX::XMMATRIX
#define float4 DirectX::XMFLOAT4
#define float3 DirectX::XMFLOAT3
#define uint unsigned int
#endif

// Shared Constant Buffer Struct
struct ShaderParams {
  float4x4 viewInverse;
  float4x4 projInverse;
  float4 cameraPos;
  float4 lightPos;
  uint maxBounces;
  float emissiveIntensity;
  float animationTime;
  float padding;
};

#ifdef HLSL
// Cleanup macros if any
#else
#undef float4x4
#undef float4
#undef float3
#undef uint
#endif

#endif
