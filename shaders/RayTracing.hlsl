#define HLSL
#include "RayTracingHlslCompat.h"

RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0, space0);

cbuffer CameraParams : register(b0)
{
    ShaderParams cb;
}

struct RayPayload
{
    float4 color;
    float3 hitNormal;
    float3 hitPos;
    uint   instanceID;
    bool   didHit;
};

float3 ComputeLighting(float3 hitPos, float3 normal, float3 baseColor, float3 lightPos)
{
    float3 lightDir = normalize(lightPos - hitPos);
    float diff = max(dot(normal, lightDir), 0.0);
    return baseColor * (diff * 0.8 + 0.2); // Diffuse + Ambient
}

float3 GetPastelColor(uint instanceID)
{
    uint h = instanceID * 0x9E3779B9u;
    h = ((h >> 16) ^ h) * 0x45D9F3B;
    h = ((h >> 16) ^ h) * 0x45D9F3B;
    h = (h >> 16) ^ h;
    
    float r = float(h & 0xFF) / 255.0;
    float g = float((h >> 8) & 0xFF) / 255.0;
    float b = float((h >> 16) & 0xFF) / 255.0;
    
    return lerp(float3(r, g, b), float3(1.0, 1.0, 1.0), 0.5);
}

[shader("raygeneration")]
void RayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim = DispatchRaysDimensions();

    float2 crd = float2(launchIndex.xy) / float2(launchDim.xy) * 2.0 - 1.0;
    crd.y = -crd.y;

    float4 origin = mul(viewInverse, float4(0,0,0,1));
    float4 target = mul(projInverse, float4(crd.x, crd.y, 1, 1));
    float4 direction = mul(viewInverse, float4(normalize(target.xyz), 0));

    RayDesc ray;
    ray.Origin = origin.xyz;
    ray.Direction = normalize(direction.xyz);
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    float3 finalColor = float3(0, 0, 0);
    float3 throughput = float3(1, 1, 1);
    
    // Iterative ray tracing loop (instead of recursive)
    for (int bounce = 0; bounce < (int)cb.maxBounces; ++bounce)
    {
        RayPayload payload = (RayPayload)0;
        payload.didHit = false;
        
        TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);
        
        if (!payload.didHit)
        {
            // Sky color
            float3 dir = normalize(ray.Direction);
            float t = 0.5 * (dir.y + 1.0);
            float3 skyColor = lerp(float3(1.0, 1.0, 1.0), float3(0.5, 0.7, 1.0), t);
            finalColor += throughput * skyColor;
            break;
        }
        
        float3 baseColor;
        bool isReflective = false;
        
        if (payload.instanceID == 0) // Floor
        {
            float scale = 0.5;
            float3 p = floor(payload.hitPos * scale);
            bool check = fmod(p.x + p.y + p.z, 2.0) == 0.0;
            baseColor = check ? float3(0.9, 0.9, 0.9) : float3(0.5, 0.5, 0.5);
        }
        else if (payload.instanceID == 1) // Main Metal Sphere
        {
            baseColor = float3(0.8, 0.8, 0.9); // Slight tint for the reflective sphere
            isReflective = true;
        }
        else // Pastel Balls
        {
            baseColor = GetPastelColor(payload.instanceID);
        }
        
        float3 litColor = ComputeLighting(payload.hitPos, payload.hitNormal, baseColor, cb.lightPos.xyz);
        
        if (isReflective)
        {
            // Add some direct color and continue reflecting
            finalColor += throughput * litColor * 0.1; // 10% direct
            throughput *= 0.8; // 80% reflection
            
            // Set up next ray
            ray.Origin = payload.hitPos + payload.hitNormal * 0.01;
            ray.Direction = reflect(ray.Direction, payload.hitNormal);
            ray.TMin = 0.001;
            ray.TMax = 10000.0;
        }
        else
        {
            // Non-reflective surface
            float3 emissive = float3(0, 0, 0);
            if (payload.instanceID > 1) // Only pastel balls are emissive
            {
                emissive = baseColor * cb.emissiveIntensity * 0.5;
            }
            
            finalColor += throughput * (litColor + emissive);
            break;
        }
    }

    RenderTarget[launchIndex.xy] = float4(finalColor, 1.0);
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    payload.didHit = false;
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.didHit = true;
    payload.hitPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    payload.instanceID = InstanceID();
    
    // Compute normal
    float3 objPos = ObjectRayOrigin() + ObjectRayDirection() * RayTCurrent();
    
    if (payload.instanceID > 0) // Spheres
    {
        float3x4 objToWorld = ObjectToWorld3x4();
        payload.hitNormal = normalize(objPos);
        payload.hitNormal = normalize(mul((float3x3)objToWorld, payload.hitNormal));
    }
    else // Plane
    {
        payload.hitNormal = float3(0, 1, 0);
    }
}
