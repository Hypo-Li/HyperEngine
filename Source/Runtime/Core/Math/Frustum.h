#pragma once
#include <array>
#include <immintrin.h>

#include "Plane.h"
#include "Math.h"
#include "Box.h"
#include "Sphere.h"

namespace hyp
{
    enum class FrustumPlane
    {
        Left,
        Right,
        Bottom,
        Top,
        Near,
        Far,
    };

    template <typename T>
    class TFrustum
    {
    public:
        TFrustum() noexcept = default;

        TFrustum(const TMatrix4<T>& m) noexcept
        {
            T m00 = m[0][0];
            T m01 = m[0][1];
            T m02 = m[0][2];
            T m03 = m[0][3];
            T m10 = m[1][0];
            T m11 = m[1][1];
            T m12 = m[1][2];
            T m13 = m[1][3];
            T m20 = m[2][0];
            T m21 = m[2][1];
            T m22 = m[2][2];
            T m23 = m[2][3];
            T m30 = m[3][0];
            T m31 = m[3][1];
            T m32 = m[3][2];
            T m33 = m[3][3];

            {
                // Left Plane (Row3 + Row0)
                float3 normal = float3(m03 + m00, m13 + m10, m23 + m20);
                float invLength = 1.0f / Math::length(normal);
                mPlanes[0] = TPlane<T>(
                    normal * invLength,
                    (m33 + m30) * invLength
                );
            }

            {
                // Right Plane (Row3 - Row0)
                float3 normal = float3(m03 - m00, m13 - m10, m23 - m20);
                float invLength = 1.0f / Math::length(normal);
                mPlanes[1] = TPlane<T>(
                    normal * invLength,
                    (m33 - m30) * invLength
                );
            }

            {
                // Bottom Plane (Row3 + Row1)
                float3 normal = float3(m03 + m01, m13 + m11, m23 + m21);
                float invLength = 1.0f / Math::length(normal);
                mPlanes[2] = TPlane<T>(
                    normal * invLength,
                    (m33 + m31) * invLength
                );
            }

            {
                // Top Plane (Row3 - Row1)
                float3 normal = float3(m03 - m01, m13 - m11, m23 - m21);
                float invLength = 1.0f / Math::length(normal);
                mPlanes[3] = TPlane<T>(
                    normal * invLength,
                    (m33 - m31) * invLength
                );
            }
            
            {
                // 针对 [0, 1] 范围的深度平面
                // 平面 A (z=w): 在普通矩阵中是 Far，在 Reversed-Z 中是 Near
                float3 normal = float3(m03 - m02, m13 - m12, m23 - m22);
                float invLength = 1.0f / Math::length(normal);
                mPlanes[4] = TPlane<T>(
                    normal * invLength,
                    (m33 - m32) * invLength
                );
            }

            {
                // 平面 B (z=0): 在普通矩阵中是 Near，在 Reversed-Z 中是 Far
                float3 normal = float3(m02, m12, m22);
                float invLength = 1.0f / Math::length(normal);
                mPlanes[5] = TPlane<T>(
                    normal * invLength,
                    m32 * invLength
                );
            }

            if (std::isnan(glm::length(mPlanes[5].getNormal())))
            {
                mPlanes[5] = TPlane<T>(
                    float3(0, 0, 0),
                    std::numeric_limits<T>::max()
                );
            }
        }

        bool contains(const TBox<T>& box) const noexcept
        {
            auto center = box.getCenter();
            auto extent = box.getExtent();

            for (const auto& plane : mPlanes)
            {
                const TVector3<T> normal = static_cast<TVector3<T>>(plane.getNormal());

                // 计算投影半径 r
                const T r = extent.x * std::abs(normal.x) +
                    extent.y * std::abs(normal.y) +
                    extent.z * std::abs(normal.z);

                const T distanceToCenter = Math::dot(normal, center) + plane.getDistance();

                // 计算中心点到平面的距离
                if (distanceToCenter < -r)
                    return false;
            }
            return true;
        }

        bool contains(const TSphere<T>& sphere) const noexcept
        {
            const auto& center = sphere.getCenter();
            const T radius = sphere.getRadius();

            for (const auto& plane : mPlanes)
            {
                const TVector3<T> normal = static_cast<TVector3<T>>(plane.getNormal());
                const T distance = plane.getDistance();

                if (Math::dot(normal, center) + distance < -radius)
                    return false;
            }
            return true;
        }

    private:
        std::array<TPlane<T>, 6> mPlanes;
    };

    template <typename T>
    struct SimdTraits;

    template <>
    struct SimdTraits<float>
    {
        using Vec = __m256;
        static constexpr int Step = 8;
        static Vec load(const float* p) { return _mm256_load_ps(p); }
        static Vec set1(float v) { return _mm256_set1_ps(v); }
        static Vec setzero() { return _mm256_setzero_ps(); }
        static Vec add(Vec a, Vec b) { return _mm256_add_ps(a, b); }
        static Vec sub(Vec a, Vec b) { return _mm256_sub_ps(a, b); }
        static Vec mul(Vec a, Vec b) { return _mm256_mul_ps(a, b); }
        static Vec abs(Vec a) { return _mm256_andnot_ps(_mm256_set1_ps(-0.0f), a); }
        static Vec bitwise_or(Vec a, Vec b) { return _mm256_or_ps(a, b); }
        static int movemask(Vec a) { return _mm256_movemask_ps(a); }
        static Vec cmp_lt(Vec a, Vec b) { return _mm256_cmp_ps(a, b, _CMP_LT_OQ); }
    };

    template <>
    struct SimdTraits<double> {
        using Vec = __m256d;
        static constexpr int Step = 4;
        static Vec load(const double* p) { return _mm256_load_pd(p); }
        static Vec load(const float* p)
        {
            __m128 f_vec = _mm_load_ps(p);
            return _mm256_cvtps_pd(f_vec);
        }
        static Vec set1(double v) { return _mm256_set1_pd(v); }
        static Vec setzero() { return _mm256_setzero_pd(); }
        static Vec add(Vec a, Vec b) { return _mm256_add_pd(a, b); }
        static Vec mul(Vec a, Vec b) { return _mm256_mul_pd(a, b); }
        static Vec sub(Vec a, Vec b) { return _mm256_sub_pd(a, b); }
        static Vec abs(Vec a) { return _mm256_andnot_pd(_mm256_set1_pd(-0.0), a); }
        static Vec bitwise_or(Vec a, Vec b) { return _mm256_or_pd(a, b); }
        static int movemask(Vec a) { return _mm256_movemask_pd(a); }
        static Vec cmp_lt(Vec a, Vec b) { return _mm256_cmp_pd(a, b, _CMP_LT_OQ); }
    };

    template <typename T>
    struct TBoxBlock
    {
        static constexpr int Step = SimdTraits<T>::Step;
        alignas(32) T cx[Step];
        alignas(32) T cy[Step];
        alignas(32) T cz[Step];
        alignas(32) T ex[Step];
        alignas(32) T ey[Step];
        alignas(32) T ez[Step];
    };

    template <typename T>
    struct TSphereBlock
    {
        static constexpr int Step = SimdTraits<T>::Step;
        alignas(32) T cx[Step];
        alignas(32) T cy[Step];
        alignas(32) T cz[Step];
        alignas(32) T r[Step];
    };

    template <typename T>
    class alignas(32) TFrustumSIMD
    {
        using ST = SimdTraits<T>;
        using Vec = typename ST::Vec;

        static constexpr int kFullMask = (1 << ST::Step) - 1;
    public:
        TFrustumSIMD() noexcept = default;

        TFrustumSIMD(const TMatrix4<T>& m) noexcept
        {
            T m00 = m[0][0];
            T m01 = m[0][1];
            T m02 = m[0][2];
            T m03 = m[0][3];
            T m10 = m[1][0];
            T m11 = m[1][1];
            T m12 = m[1][2];
            T m13 = m[1][3];
            T m20 = m[2][0];
            T m21 = m[2][1];
            T m22 = m[2][2];
            T m23 = m[2][3];
            T m30 = m[3][0];
            T m31 = m[3][1];
            T m32 = m[3][2];
            T m33 = m[3][3];

            TPlane<T> planes[6];
            {
                float3 normal = float3(m03 + m00, m13 + m10, m23 + m20);
                float invLength = 1.0f / Math::length(normal);
                planes[0] = TPlane<T>(
                    normal * invLength,
                    (m33 + m30) * invLength
                );
            }

            {
                float3 normal = float3(m03 - m00, m13 - m10, m23 - m20);
                float invLength = 1.0f / Math::length(normal);
                planes[1] = TPlane<T>(
                    normal * invLength,
                    (m33 - m30) * invLength
                );
            }

            {
                float3 normal = float3(m03 + m01, m13 + m11, m23 + m21);
                float invLength = 1.0f / Math::length(normal);
                planes[2] = TPlane<T>(
                    normal * invLength,
                    (m33 + m31) * invLength
                );
            }

            {
                float3 normal = float3(m03 - m01, m13 - m11, m23 - m21);
                float invLength = 1.0f / Math::length(normal);
                planes[3] = TPlane<T>(
                    normal * invLength,
                    (m33 - m31) * invLength
                );
            }

            {
                float3 normal = float3(m03 - m02, m13 - m12, m23 - m22);
                float invLength = 1.0f / Math::length(normal);
                planes[4] = TPlane<T>(
                    normal * invLength,
                    (m33 - m32) * invLength
                );
            }

            {
                float3 normal = float3(m02, m12, m22);
                float invLength = 1.0f / Math::length(normal);
                planes[5] = TPlane<T>(
                    normal * invLength,
                    m32 * invLength
                );
            }

            if (std::isnan(glm::length(planes[5].getNormal())))
            {
                planes[5] = TPlane<T>(
                    float3(0, 0, 0),
                    std::numeric_limits<T>::max()
                );
            }

            for (int i = 0; i < 6; ++i)
            {
                mPlaneX[i] = planes[i].getNormal().x;
                mPlaneY[i] = planes[i].getNormal().y;
                mPlaneZ[i] = planes[i].getNormal().z;
                mPlaneD[i] = planes[i].getDistance();
            }

            for (int i = 6; i < 8; ++i)
            {
                mPlaneX[i] = 0;
                mPlaneY[i] = 0;
                mPlaneZ[i] = 0;
                mPlaneD[i] = std::numeric_limits<T>::max();
            }
        }

        bool contains(const TBox<T>& box) const noexcept
        {
            auto center = box.getCenter();
            auto extent = box.getExtent();

            const auto cx = ST::set1(center.x);
            const auto cy = ST::set1(center.y);
            const auto cz = ST::set1(center.z);
            const auto ex = ST::set1(extent.x);
            const auto ey = ST::set1(extent.y);
            const auto ez = ST::set1(extent.z);

            for (int i = 0; i < 8; i += ST::Step)
            {
                auto nx = ST::load(&mPlaneX[i]);
                auto ny = ST::load(&mPlaneY[i]);
                auto nz = ST::load(&mPlaneZ[i]);
                auto d = ST::load(&mPlaneD[i]);

                auto r0 = ST::mul(ST::abs(nx), ex);
                auto r1 = ST::mul(ST::abs(ny), ey);
                auto r2 = ST::mul(ST::abs(nz), ez);
                auto r = ST::add(ST::add(r0, r1), r2);

                auto d0 = ST::mul(nx, cx);
                auto d1 = ST::mul(ny, cy);
                auto d2 = ST::mul(nz, cz);
                auto dist = ST::add(ST::add(d0, d1), ST::add(d2, d));

                auto isOutside = ST::cmp_lt(ST::add(dist, r), ST::setzero());
                if (ST::movemask(isOutside) != 0)
                    return false;
            }
            return true;
        }

        bool contains(const TSphere<T>& sphere) const noexcept
        {
            const auto& center = sphere.getCenter();
            const auto cx = ST::set1(center.x);
            const auto cy = ST::set1(center.y);
            const auto cz = ST::set1(center.z);

            const auto r = ST::set1(sphere.getRadius());
            const auto negR = ST::set1(-sphere.getRadius());

            for (int i = 0; i < 8; i += ST::Step)
            {
                auto nx = ST::load(&mPlaneX[i]);
                auto ny = ST::load(&mPlaneY[i]);
                auto nz = ST::load(&mPlaneZ[i]);
                auto d = ST::load(&mPlaneD[i]);

                auto d0 = ST::mul(nx, cx);
                auto d1 = ST::mul(ny, cy);
                auto d2 = ST::mul(nz, cz);
                auto dist = ST::add(ST::add(d0, d1), ST::add(d2, d));

                auto isOutside = ST::cmp_lt(dist, negR);
                if (ST::movemask(isOutside) != 0)
                    return false;
            }
            return true;
        }

        uint8_t contains(const TBoxBlock<T>& block) const noexcept
        {
            auto cx = ST::load(block.cx);
            auto cy = ST::load(block.cy);
            auto cz = ST::load(block.cz);
            auto ex = ST::load(block.ex);
            auto ey = ST::load(block.ey);
            auto ez = ST::load(block.ez);

            auto outMask = ST::setzero();

            for (int i = 0; i < 6; ++i)
            {
                auto nx = ST::set1(mPlaneX[i]);
                auto ny = ST::set1(mPlaneY[i]);
                auto nz = ST::set1(mPlaneZ[i]);
                auto d = ST::set1(mPlaneD[i]);

                auto abs_nx = ST::abs(nx);
                auto abs_ny = ST::abs(ny);
                auto abs_nz = ST::abs(nz);

                auto r0 = ST::mul(abs_nx, ex);
                auto r1 = ST::mul(abs_ny, ey);
                auto r2 = ST::mul(abs_nz, ez);
                auto r = ST::add(ST::add(r0, r1), r2);

                auto d0 = ST::mul(nx, cx);
                auto d1 = ST::mul(ny, cy);
                auto d2 = ST::mul(nz, cz);
                auto dist = ST::add(ST::add(d0, d1), ST::add(d2, d));

                auto isOutsideThisPlane = ST::cmp_lt(ST::add(dist, r), ST::setzero());

                outMask = ST::bitwise_or(outMask, isOutsideThisPlane);

                // Early Out: 如果这批 8 个盒子都已经确定被剔除了，直接中断平面的遍历
                if (ST::movemask(outMask) == kFullMask)
                    return 0;
            }

            return static_cast<uint8_t>(~ST::movemask(outMask) & kFullMask);
        }

        uint8_t contains(const TSphereBlock<T>& block) const noexcept
        {
            auto cx = ST::load(block.cx);
            auto cy = ST::load(block.cy);
            auto cz = ST::load(block.cz);
            auto r = ST::load(block.r);
            auto negR = ST::sub(ST::setzero(), r);

            auto outMask = ST::setzero();

            for (int i = 0; i < 6; ++i)
            {
                auto nx = ST::set1(mPlaneX[i]);
                auto ny = ST::set1(mPlaneY[i]);
                auto nz = ST::set1(mPlaneZ[i]);
                auto d = ST::set1(mPlaneD[i]);

                auto d0 = ST::mul(nx, cx);
                auto d1 = ST::mul(ny, cy);
                auto d2 = ST::mul(nz, cz);
                auto dist = ST::add(ST::add(d0, d1), ST::add(d2, d));

                auto isOutsideThisPlane = ST::cmp_lt(dist, negR);
                outMask = ST::bitwise_or(outMask, isOutsideThisPlane);

                if (ST::movemask(outMask) == kFullMask)
                    return 0;
            }

            return static_cast<uint8_t>(~ST::movemask(outMask) & kFullMask);
        }

    private:
        alignas(32) float mPlaneX[8];
        alignas(32) float mPlaneY[8];
        alignas(32) float mPlaneZ[8];
        alignas(32) T mPlaneD[8];
    };

    using Frustumf = TFrustum<float>;
    using Frustumd = TFrustum<double>;
}
