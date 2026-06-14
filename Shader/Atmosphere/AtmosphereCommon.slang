#pragma once
#include "../Common/Math.slang"
#include "../Common/WorldCoordinate.slang"

#define RAY_MARCHING_MIN_SPP 4
#define RAY_MARCHING_MAX_SPP 32
#define PLANET_RADIUS_OFFSET 0.001
#define SLICE_COUNT 32
#define KM_PER_SLICE 4

struct AtmosphereParameters
{
    float3 rayleighScattering;
    float rayleighScaleHeight;
    float3 mieScattering;
    float mieScaleHeight;
    float3 mieAbsorption;
    float miePhaseG;
    float3 ozoneAbsorption;
    float ozoneHeight;
    float ozoneThickness;
    float multiScatteringFactor;
    float bottomRadiusKm;
    float topRadiusKm;
    float3 groundAlbedo;
    float padding0;
    float3 sun0Direction;
    float padding1;
    float3 sun0Intensity;
    float padding2;
    float3 sun1Direction;
    float padding3;
    float3 sun1Intensity;
};

float3 getWorldPos(in AtmosphereParameters atmosphere, in WorldCoord localPos)
{
    float3 localPosKm = localPos.tile * (WORLD_TILE_SIZE * 0.001) + localPos.offset * 0.001;
    float3 worldPos = localPosKm + float3(0, 0, atmosphere.bottomRadiusKm);
    float viewHeight = length(worldPos);
    float3 upVector = worldPos / viewHeight;
    return upVector * max(viewHeight, atmosphere.bottomRadiusKm + 0.005);
}

// return distance from ro to closest intersection with sphere
// or -1 if no intersection
float rayIntersectSphere(float3 ro, float3 rd, float4 sphere)
{
    float3 localPos = ro - sphere.xyz;
    float a = dot(rd, rd);
    float b = 2.0 * dot(rd, localPos);
    float c = dot(localPos, localPos) - (sphere.w * sphere.w);
    float delta = b * b - 4.0 * a * c;
    if (delta < 0.0 || a == 0.0)
        return -1.0;
    float sol0 = (-b - sqrt(delta)) / (2.0 * a);
    float sol1 = (-b + sqrt(delta)) / (2.0 * a);
    if (sol0 < 0.0 && sol1 < 0.0)
        return -1.0;
    if (sol0 < 0.0)
        return max(0.0, sol1);
    else if (sol1 < 0.0)
        return max(0.0, sol0);
    return max(0.0, min(sol0, sol1));
}

bool rayIntersectSphereSolution(in float3 ro, in float3 rd, in float4 sphere, out float2 solutions)
{
    float3 localPosition = ro - sphere.xyz;
    float localPositionSqr = dot(localPosition, localPosition);

    float3 quadraticCoef;
    quadraticCoef.x = dot(rd, rd);
	quadraticCoef.y = 2 * dot(rd, localPosition);
	quadraticCoef.z = localPositionSqr - sphere.w * sphere.w;

	float discriminant = quadraticCoef.y * quadraticCoef.y - 4 * quadraticCoef.x * quadraticCoef.z;

	if (discriminant >= 0)
	{
		float sqrtDiscriminant = sqrt(discriminant);
		solutions = (-quadraticCoef.y + float2(-1, 1) * sqrtDiscriminant) / (2 * quadraticCoef.x);
		return true;
	}
    solutions = float2(0.0);
	return false;
}

void uvToTransmittanceLutParameters(in AtmosphereParameters atmosphere, float2 uv,  out float viewHeight, out float viewZenithCos)
{
    const float topRadiusKm = atmosphere.topRadiusKm;
    const float bottomRadiusKm = atmosphere.bottomRadiusKm;
    const float H = sqrt(topRadiusKm * topRadiusKm - bottomRadiusKm * bottomRadiusKm);
    const float rho = H * uv.y;
    viewHeight = sqrt(rho * rho + bottomRadiusKm * bottomRadiusKm);
    const float dMin = topRadiusKm - viewHeight;
    const float dMax = rho + H;
    const float d = dMin + uv.x * (dMax - dMin);
    viewZenithCos = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * viewHeight * d);
    viewZenithCos = clamp(viewZenithCos, -1.0, 1.0);
}

void transmittanceLutParametersToUV(in AtmosphereParameters atmosphere, float viewHeight, float viewZenithCos, out float2 uv)
{
    const float topRadiusKm = atmosphere.topRadiusKm;
    const float bottomRadiusKm = atmosphere.bottomRadiusKm;
    const float H = sqrt(max(0.0f, topRadiusKm * topRadiusKm - bottomRadiusKm * bottomRadiusKm));
    const float rho = sqrt(max(0.0f, viewHeight * viewHeight - bottomRadiusKm * bottomRadiusKm));
    const float discriminant = viewHeight * viewHeight * (viewZenithCos * viewZenithCos - 1.0) + topRadiusKm * topRadiusKm;
    const float d = max(0.0, (-viewHeight * viewZenithCos + sqrt(discriminant))); // Distance to atmosphere boundary
    const float dMin = topRadiusKm - viewHeight;
    const float dMax = rho + H;
    const float u = (d - dMin) / (dMax - dMin);
    const float v = rho / H;
    uv = float2(u, v); 
}

void uvToSkyViewLutParameters(in AtmosphereParameters atmosphere, float viewHeight, float2 uv, out float viewZenithCos, out float lightViewCos)
{
    static constexpr float4 sizeAndInvSize = float4(192, 104, 1.0 / 192.0, 1.0 / 104.0);
    uv = (uv - 0.5 * sizeAndInvSize.zw) * (sizeAndInvSize.xy / (sizeAndInvSize.xy - 1.0));

    float vHorizon = sqrt(viewHeight * viewHeight - atmosphere.bottomRadiusKm * atmosphere.bottomRadiusKm);
    float cosBeta = float(vHorizon / viewHeight);
    float beta = acos(cosBeta);
    float zenithHorizonAngle = PI - beta;
    if (uv.y < 0.5)
    {
        float coord = 2.0 * uv.y;
        coord = 1.0 - coord;
        coord *= coord;
        coord = 1.0 - coord;
        viewZenithCos = cos(zenithHorizonAngle * coord);
    }
    else
    {
        float coord = uv.y * 2.0 - 1.0;
        coord *= coord;
        viewZenithCos = cos(zenithHorizonAngle + beta * coord);
    }
    float coord = uv.x;
    coord *= coord;
    lightViewCos = -(coord * 2.0 - 1.0);
}

void skyViewLutParametersToUV(in AtmosphereParameters atmosphere, bool intersectGround, float viewZenithCos, float lightViewCos, float viewHeight, out float2 uv)
{
    float vHorizon = sqrt(viewHeight * viewHeight - atmosphere.bottomRadiusKm * atmosphere.bottomRadiusKm);
    float cosBeta = float(vHorizon / viewHeight); // GroundToHorizonCos
    float beta = acos(cosBeta);
    float zenithHorizonAngle = PI - beta;
    const float viewZenithAngle = acos(viewZenithCos);
    if (!intersectGround)
    {
        float coord = viewZenithAngle / zenithHorizonAngle;
        coord = 1.0 - coord;
        coord = sqrt(coord);
        coord = 1.0 - coord;
        uv.y = coord * 0.5;
    }
    else
    {
        float coord = (viewZenithAngle - zenithHorizonAngle) / beta;
        coord = sqrt(coord);
        uv.y = coord * 0.5 + 0.5;
    }
    float coord = -lightViewCos * 0.5 + 0.5;
    coord = sqrt(coord);
    uv.x = coord;
}

float3 getTransmittance(in AtmosphereParameters atmosphere, in Sampler2D transmittanceLut, float lightZenithCos, float height)
{
    float2 uv;
    transmittanceLutParametersToUV(atmosphere, height, lightZenithCos, uv);
    return transmittanceLut.SampleLevel(uv, 0.0).rgb;
}

float3 getMultiScattering(in AtmosphereParameters atmosphere, in Sampler2D multiScatteringLut, float lightZenithCos, float height)
{
    float2 uv = float2(
        lightZenithCos * 0.5 + 0.5,
        (height - atmosphere.bottomRadiusKm) / (atmosphere.topRadiusKm - atmosphere.bottomRadiusKm)
    );
    return multiScatteringLut.SampleLevel(uv, 0.0).rgb * atmosphere.multiScatteringFactor;
}

float aerialPerspectiveDepthToSlice(float depth)
{
    return depth * (1.0f / KM_PER_SLICE);
}

float aerialPerspectiveSliceToDepth(float slice)
{
    return slice * KM_PER_SLICE;
}

void getScatteringAndExtinction(in AtmosphereParameters atmosphere, float3 worldPos, out float3 rayleighScattering, out float3 mieScattering, out float3 extinction)
{
    const float altitude = length(worldPos) - atmosphere.bottomRadiusKm;
    const float rayleighDensity = exp(-altitude / atmosphere.rayleighScaleHeight);
    rayleighScattering = rayleighDensity * atmosphere.rayleighScattering;
    const float mieDensity = exp(-altitude / atmosphere.mieScaleHeight);
    mieScattering = mieDensity * atmosphere.mieScattering;
    const float3 mieAbsorption = mieDensity * atmosphere.mieAbsorption;
    const float3 ozoneAbsorption = max(0.0, 1.0 - abs(altitude - atmosphere.ozoneHeight) / atmosphere.ozoneThickness) * atmosphere.ozoneAbsorption;
    extinction = rayleighScattering + mieScattering + mieAbsorption + ozoneAbsorption;
}

float3 getOpticalDepth(in AtmosphereParameters atmosphere, float3 worldPos, float3 worldDir, float sampleCount)
{
    if (rayIntersectSphere(worldPos, worldDir, float4(0, 0, 0, atmosphere.bottomRadiusKm)) > 0.0)
        return float3(0);
    float atmoDist = rayIntersectSphere(worldPos, worldDir, float4(0, 0, 0, atmosphere.topRadiusKm));
    const float dt = atmoDist / sampleCount;
    float t = 0.0;
    float3 opticalDepth = float3(0);
    for (float s = 0.0; s < sampleCount; s += 1.0)
    {
        t = ((s + 0.3) / sampleCount) * atmoDist;
        float3 newPos = worldPos + t * worldDir;
        float3 rayleighScattering, mieScattering, extinction;
        getScatteringAndExtinction(atmosphere, newPos, rayleighScattering, mieScattering, extinction);
        opticalDepth += dt * extinction;
    }
    return opticalDepth;
}

float HenyeyGreensteinPhase(float g, float cosTheta)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * denom * sqrt(denom));
}

float dualLobPhase(float g1, float g2, float w, float cosTheta)
{
    return lerp(HenyeyGreensteinPhase(g1, cosTheta), HenyeyGreensteinPhase(g2, cosTheta), w);
}

float RayleighPhase(float cosTheta)
{
    float k = 3.0 / (16.0 * PI);
    return k * (1.0 + cosTheta * cosTheta);
}

bool moveToTopAtmosphere(in AtmosphereParameters atmosphere, inout float3 worldPos, in float3 worldDir)
{
    float viewHeight = length(worldPos);
    if (viewHeight > atmosphere.topRadiusKm)
    {
        float tTop = rayIntersectSphere(worldPos, worldDir, float4(0, 0, 0, atmosphere.topRadiusKm));
        if (tTop >= 0.0)
        {
            float3 upVector = float3(worldPos / viewHeight);
            float3 upOffset = upVector * -(PLANET_RADIUS_OFFSET * 100.0);
            worldPos = worldPos + worldDir * tTop + upOffset;
        }
        else
            // Ray is not intersecting the atmosphere
            return false;
    }
    return true; // ok to start tracing
}

void rayMarchAtmosphere(
    in AtmosphereParameters atmosphere,
    in Sampler2D transmittanceLut,
#ifdef MULTI_SCATTERING_APPROX_SAMPLING_ENABLED
    in Sampler2D multiScatteringLut,
#endif
    float3 worldPos,
    float3 worldDir,
    float3 light0Dir,
    float pixelNoise,
    float sampleCount,
    float tMaxMax,
#ifdef RAY_MARCHING_WITH_DEPTH
    float sceneDepth,
#endif
    out float3 lum,
    out float3 fms,
    out float3 trans
)
{
    lum = float3(0);
    fms = float3(0);
    trans = float3(1);
    float tBottom = rayIntersectSphere(worldPos, worldDir, float4(0, 0, 0, atmosphere.bottomRadiusKm));
    float tTop = rayIntersectSphere(worldPos, worldDir, float4(0, 0, 0, atmosphere.topRadiusKm));
    float tMax = 0.0;

    if (tTop < 0)
        return;
    else if (tBottom < 0)
        tMax = tTop;
    else
        tMax = max(0.0, tBottom);
#ifdef RAY_MARCHING_WITH_DEPTH
    if (sceneDepth > 0.0)
        tMax = min(sceneDepth, tmax);
#endif
    tMax = min(tMax, tMaxMax);

#ifdef VARIABLE_SAMPLE_COUNT_ENABLED
    constexpr float kDistanceToSampleCountMaxInv = 1.0 / 150.0;
    sampleCount = lerp(RAY_MARCHING_MIN_SPP, RAY_MARCHING_MAX_SPP, clamp(tMax * kDistanceToSampleCountMaxInv, 0.0, 1.0));
    float sampleCountFloor = floor(sampleCount);
    float tMaxFloor = tMax * sampleCountFloor / sampleCount;
#endif
    constexpr float uniformPhase = 1.0 / (4.0 * PI);
    const float cosTheta = dot(light0Dir, worldDir);
    const float miePhaseValueLight0 = HenyeyGreensteinPhase(atmosphere.miePhaseG, cosTheta);
    const float rayleighPhaseValueLight0 = RayleighPhase(cosTheta);
    float t = 0.0;
    float dt = tMax / sampleCount;
    for (float s = 0.0; s < sampleCount; s += 1.0)
    {
#ifdef VARIABLE_SAMPLE_COUNT_ENABLED
        float t0 = s / sampleCountFloor;
        float t1 = (s + 1.0) / sampleCountFloor;
        t0 *= t0;
        t1 *= t1;
        t0 = tMaxFloor * t0;
        t1 = t1 > 1.0 ? tMax : tMaxFloor * t1;
        t = t0 + (t1 - t0) * pixelNoise;
        dt = t1 - t0;
#else
        t = tMax * (s + pixelNoise) / sampleCount;
#endif
        float3 newPos = worldPos + t * worldDir;
        float3 rayleighScattering, mieScattering, extinction;
        getScatteringAndExtinction(atmosphere, newPos, rayleighScattering, mieScattering, extinction);
        float3 scattering = rayleighScattering + mieScattering;
        float3 sampleTransmittance = exp(-extinction * dt);
        float viewHeight = length(newPos);
        float3 upVector = newPos / viewHeight;
        float light0ZenithCos = dot(light0Dir, upVector);
        float3 transmittanceToLight0 = getTransmittance(atmosphere, transmittanceLut, light0ZenithCos, viewHeight);
#ifdef MIE_RAYLEIGH_PHASE_ENABLED
        float3 phaseTimesScattering = mieScattering * miePhaseValueLight0 + rayleighScattering * rayleighPhaseValueLight0;
#else
        float3 phaseTimesScattering = scattering * uniformPhase;
#endif
        float tEarth = rayIntersectSphere(newPos, light0Dir, float4(PLANET_RADIUS_OFFSET * upVector, atmosphere.bottomRadiusKm));
        float earthShadow = step(tEarth, 0.0);
#ifdef MULTI_SCATTERING_APPROX_SAMPLING_ENABLED
        float3 multiScattering = getMultiScattering(atmosphere, multiScatteringLut, light0ZenithCos, viewHeight);
#else
        float3 multiScattering = float3(0);
#endif

#ifdef PREMULTIPLY_LIGHTING
        constexpr float3 lightingColor = atmosphere.sun0Intensity;
#else
        constexpr float3 lightingColor = float3(1);
#endif
        float3 S = (earthShadow * transmittanceToLight0 * phaseTimesScattering + multiScattering * scattering) * lightingColor;
        
        const float3 safeMediumExtinction = max(extinction, float3(1e-9));

        // L_segment = ∫_0^dt(S * e^(-σ_t * x)) = (S * (1 - e^(-σ_t * dt))) / σ_t
        lum += trans * (S - S * sampleTransmittance) / safeMediumExtinction;

        // see UE's SkyAtmosphere.usf
        // When using the power serie to accumulate all sattering order, serie r must be <1 for a serie to converge. 
		// Under extreme coefficient, MultiScatAs1 can grow larger and thus results in broken visuals. 
		// The way to fix that is to use a proper analytical integration as porposed in slide 28 of http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/ 
		// However, it is possible to disable as it can also work using simple power serie sum unroll up to 5th order. The rest of the orders has a really low contribution. 
#define MULTI_SCATTERING_POWER_SERIE 0
#if MULTI_SCATTERING_POWER_SERIE
        fms += trans * (scattering - scattering * sampleTransmittance) / safeMediumExtinction;
#else
        // 1 is the integration of luminance over the 4pi of a sphere, and assuming an isotropic phase function of 1.0/(4*PI)
        fms += trans * scattering * 1.0 * dt;
#endif

        trans *= sampleTransmittance;
    }
#ifdef GROUND_ALBEDO_ENABLED
    if (tMax == tBottom && tBottom > 0.0)
    {
        float3 newPos = worldPos + tBottom * worldDir;
        float viewHeight = length(newPos);
        float3 upVector = newPos / viewHeight;
        float light0ZenithCos = dot(light0Dir, upVector);
        float3 transmittanceToLight0 = getTransmittance(atmosphere, transmittanceLut, light0ZenithCos, viewHeight);
        float NoL = max(light0ZenithCos, 0.0);
        lum += transmittanceToLight0 * trans * NoL * atmosphere.groundAlbedo / PI;
    }
#endif
}