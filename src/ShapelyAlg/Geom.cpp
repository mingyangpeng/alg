// Geom.cpp —— Geom 底层 GEOS 运算实现（纯 double，Pimpl）。
// 线程安全：GEOS reentrant 上下文线程局部。
//
// GEOS 所有权铁律（踩坑记录）：
//   1. createLinearRing/createPolygon/createPoint 的 CoordSeq 所有权转移给几何，
//      仅创建失败才自己 destroy；
//   2. GEOSGetExteriorRing/GEOSGetGeometryN 返回内部指针禁止 destroy，
//      绝不能包进 RAII 包装（双重释放 0xc0000374）；
//   3. LinearRing 必须闭合（首尾点相同），否则 GEOS 堆损坏（不是返回 NULL）；
//   4. GEOSGeomGetPointN 只对 LineString 合法；
//   5. 空几何 distance 返回 inf（成功），判空用 GEOSisEmpty_r。

#include "Geom.h"

#include <geos_c.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace shapely {

// ---------------- GEOS 上下文（线程局部） ----------------

static GEOSContextHandle_t ctx()
{
    thread_local GEOSContextHandle_t h = GEOS_init_r();
    return h;
}

// 构建失败自动回收的临时指针
struct GeosPtr
{
    GEOSGeometry* g = nullptr;
    explicit GeosPtr(GEOSGeometry* p) : g(p) {}
    ~GeosPtr() { if (g) GEOSGeom_destroy_r(ctx(), g); }
    GeosPtr(const GeosPtr&) = delete;
    GeosPtr& operator=(const GeosPtr&) = delete;
};

// ---------------- 生命周期 ----------------

Geom::Geom() {}
Geom::Geom(void* g) : geom_(g) {}

Geom::~Geom()
{
    if (geom_)
        GEOSGeom_destroy_r(ctx(), (GEOSGeometry*)geom_);
}

Geom::Geom(Geom&& other) noexcept : geom_(other.geom_) { other.geom_ = nullptr; }

Geom& Geom::operator=(Geom&& other) noexcept
{
    if (this != &other) {
        if (geom_)
            GEOSGeom_destroy_r(ctx(), (GEOSGeometry*)geom_);
        geom_ = other.geom_;
        other.geom_ = nullptr;
    }
    return *this;
}

// ---------------- 状态检查 ----------------

bool Geom::isEmpty() const
{
    return !geom_ || GEOSisEmpty_r(ctx(), (const GEOSGeometry*)geom_) == 1;
}

std::string Geom::wkt() const
{
    if (!geom_)
        return {};
    GEOSWKTWriter* w = GEOSWKTWriter_create_r(ctx());
    GEOSWKTWriter_setRoundingPrecision_r(ctx(), w, 15);
    char* s = GEOSWKTWriter_write_r(ctx(), w, (const GEOSGeometry*)geom_);
    GEOSWKTWriter_destroy_r(ctx(), w);
    std::string out = s ? s : "";
    if (s)
        GEOSFree_r(ctx(), s);
    return out;
}

std::string Geom::typeName() const
{
    if (!geom_)
        return "null";
    const char* s = GEOSGeomType_r(ctx(), (const GEOSGeometry*)geom_);
    return s ? s : "unknown";
}

// ---------------- 通用功能 ----------------

BoundingBox2d Geom::bounds() const
{
    BoundingBox2d bb;
    if (!geom_)
        return bb;
    if (GEOSGeom_getExtent_r(ctx(), (const GEOSGeometry*)geom_,
                             &bb.min_x, &bb.min_y, &bb.max_x, &bb.max_y) != 1)
        return BoundingBox2d();
    bb.valid = true;
    return bb;
}

std::vector<Point2d> Geom::getExteriorPoints() const
{
    std::vector<Point2d> out;
    if (!geom_)
        return out;
    const GEOSContextHandle_t c = ctx();
    const GEOSGeometry* g = (const GEOSGeometry*)geom_;

    // 顶点读取（按几何类型分派；内部指针只读 CoordSeq，不构造 RAII 包装）
    const int type = GEOSGeomTypeId_r(c, g);
    switch (type) {
    case GEOS_POINT:
    case GEOS_LINESTRING:
    case GEOS_LINEARRING: {
        const GEOSCoordSequence* s = GEOSGeom_getCoordSeq_r(c, g);
        if (!s)
            return out;
        unsigned n = 0;
        if (GEOSCoordSeq_getSize_r(c, s, &n) != 1)
            return out;
        out.reserve(n);
        for (unsigned i = 0; i < n; ++i) {
            double x = 0, y = 0;
            if (GEOSCoordSeq_getX_r(c, s, i, &x) == 1 &&
                GEOSCoordSeq_getY_r(c, s, i, &y) == 1)
                out.push_back(Point2d{ x, y });
        }
        return out;
    }
    case GEOS_POLYGON: {
        // 外壳 ring 的坐标序列（内部 ring 忽略，主轮廓足够）
        const GEOSGeometry* shell = GEOSGetExteriorRing_r(c, g);
        if (!shell)
            return out;
        const GEOSCoordSequence* s = GEOSGeom_getCoordSeq_r(c, shell);
        if (!s)
            return out;
        unsigned n = 0;
        if (GEOSCoordSeq_getSize_r(c, s, &n) != 1)
            return out;
        out.reserve(n);
        for (unsigned i = 0; i < n; ++i) {
            double x = 0, y = 0;
            if (GEOSCoordSeq_getX_r(c, s, i, &x) == 1 &&
                GEOSCoordSeq_getY_r(c, s, i, &y) == 1)
                out.push_back(Point2d{ x, y });
        }
        return out;
    }
    default: {
        // GeometryCollection / Multi*：逐子几何读 CoordSeq
        const int n = GEOSGetNumGeometries_r(c, g);
        for (int i = 0; i < n; ++i) {
            const GEOSGeometry* sub = GEOSGetGeometryN_r(c, g, i);
            if (!sub)
                continue;
            const GEOSCoordSequence* s = GEOSGeom_getCoordSeq_r(c, sub);
            if (!s)
                continue;
            unsigned m = 0;
            if (GEOSCoordSeq_getSize_r(c, s, &m) != 1)
                continue;
            for (unsigned k = 0; k < m; ++k) {
                double x = 0, y = 0;
                if (GEOSCoordSeq_getX_r(c, s, k, &x) == 1 &&
                    GEOSCoordSeq_getY_r(c, s, k, &y) == 1)
                    out.push_back(Point2d{ x, y });
            }
        }
        return out;
    }
    }
}

double Geom::distanceTo(const Geom& other) const
{
    if (!geom_ || !other.geom_)
        return -1.0;
    // GEOS 空几何（如 fromWKT("POINT EMPTY")）返回 inf——按头文件契约归一为 -1
    if (isEmpty() || other.isEmpty())
        return -1.0;
    double d = 0;
    if (GEOSDistance_r(ctx(), (const GEOSGeometry*)geom_,
                       (const GEOSGeometry*)other.geom_, &d) != 1)
        return -1.0;
    return d;
}

bool Geom::intersects(const Geom& other) const
{
    if (!geom_ || !other.geom_)
        return false;
    return GEOSIntersects_r(ctx(), (const GEOSGeometry*)geom_,
                            (const GEOSGeometry*)other.geom_) == 1;
}

// ---------------- 点专属 ----------------

double Geom::getX() const
{
    if (!geom_ || GEOSGeomTypeId_r(ctx(), (const GEOSGeometry*)geom_) != GEOS_POINT)
        return 0.0;   // 非点返回 0（头文件契约）
    double x = 0;
    if (GEOSGeom_getXMin_r(ctx(), (const GEOSGeometry*)geom_, &x) != 1)
        return 0.0;
    return x;
}

double Geom::getY() const
{
    if (!geom_ || GEOSGeomTypeId_r(ctx(), (const GEOSGeometry*)geom_) != GEOS_POINT)
        return 0.0;   // 非点返回 0（头文件契约）
    double y = 0;
    if (GEOSGeom_getYMin_r(ctx(), (const GEOSGeometry*)geom_, &y) != 1)
        return 0.0;
    return y;
}

bool Geom::within(const Geom& lineOrPolygon) const
{
    if (!geom_ || !lineOrPolygon.geom_)
        return false;
    return GEOSWithin_r(ctx(), (const GEOSGeometry*)geom_,
                        (const GEOSGeometry*)lineOrPolygon.geom_) == 1;
}

// ---------------- 线专属 ----------------

double Geom::length() const
{
    if (!geom_)
        return 0.0;
    double len = 0;
    if (GEOSLength_r(ctx(), (const GEOSGeometry*)geom_, &len) != 1)
        return 0.0;
    return len;
}

bool Geom::crosses(const Geom& other) const
{
    if (!geom_ || !other.geom_)
        return false;
    return GEOSCrosses_r(ctx(), (const GEOSGeometry*)geom_,
                         (const GEOSGeometry*)other.geom_) == 1;
}

// ---------------- 面专属 ----------------

double Geom::area() const
{
    if (!geom_)
        return 0.0;
    double a = 0;
    if (GEOSArea_r(ctx(), (const GEOSGeometry*)geom_, &a) != 1)
        return 0.0;
    return a;
}

Point2d Geom::centroid() const
{
    if (!geom_)
        return Point2d();
    // 真质心（面积加权；凹多边形质心可能在面外）
    GeosPtr c(GEOSGetCentroid_r(ctx(), (const GEOSGeometry*)geom_));
    if (!c.g)
        return Point2d();
    double x = 0, y = 0;
    GEOSGeom_getXMin_r(ctx(), c.g, &x);
    GEOSGeom_getYMin_r(ctx(), c.g, &y);
    return Point2d{ x, y };
}

bool Geom::contains(const Geom& other) const
{
    if (!geom_ || !other.geom_)
        return false;
    return GEOSContains_r(ctx(), (const GEOSGeometry*)geom_,
                          (const GEOSGeometry*)other.geom_) == 1;
}

Geom Geom::intersection(const Geom& other) const
{
    if (!geom_ || !other.geom_)
        return Geom();
    return Geom(GEOSIntersection_r(ctx(), (const GEOSGeometry*)geom_,
                                   (const GEOSGeometry*)other.geom_));
}

Geom Geom::unionWith(const Geom& other) const
{
    if (!geom_ || !other.geom_)
        return Geom();
    return Geom(GEOSUnion_r(ctx(), (const GEOSGeometry*)geom_,
                            (const GEOSGeometry*)other.geom_));
}

Geom Geom::difference(const Geom& other) const
{
    if (!geom_ || !other.geom_)
        return Geom();
    return Geom(GEOSDifference_r(ctx(), (const GEOSGeometry*)geom_,
                                 (const GEOSGeometry*)other.geom_));
}

Geom Geom::buffer(double dist, int quadSegs) const
{
    if (!geom_)
        return Geom();
    return Geom(GEOSBuffer_r(ctx(), (const GEOSGeometry*)geom_, dist, quadSegs));
}

Geom Geom::convexHull() const
{
    if (!geom_)
        return Geom();
    return Geom(GEOSConvexHull_r(ctx(), (const GEOSGeometry*)geom_));
}

BoundingBox2d Geom::toRect2d() const
{
    return bounds();
}

// ---------------- 底层构建 ----------------

Geom Geom::makePoint(double x, double y)
{
    const GEOSContextHandle_t c = ctx();
    GEOSCoordSequence* s = GEOSCoordSeq_create_r(c, 1, 2);
    if (!s)
        return Geom();
    GEOSCoordSeq_setX_r(c, s, 0, x);
    GEOSCoordSeq_setY_r(c, s, 0, y);
    GEOSGeometry* g = GEOSGeom_createPoint_r(c, s);
    if (!g)
        GEOSCoordSeq_destroy_r(c, s);
    return Geom(g);
}

Geom Geom::makeLine(const std::vector<Point2d>& pts)
{
    if (pts.size() < 2)
        return Geom();
    const GEOSContextHandle_t c = ctx();
    GEOSCoordSequence* s = GEOSCoordSeq_create_r(c, (unsigned)pts.size(), 2);
    if (!s)
        return Geom();
    for (size_t i = 0; i < pts.size(); ++i) {
        GEOSCoordSeq_setX_r(c, s, (unsigned)i, pts[i].x);
        GEOSCoordSeq_setY_r(c, s, (unsigned)i, pts[i].y);
    }
    GEOSGeometry* g = GEOSGeom_createLineString_r(c, s);
    if (!g)
        GEOSCoordSeq_destroy_r(c, s);
    return Geom(g);
}

// 多边形：自动闭合（LinearRing 要求首尾相同，不闭合会堆损坏）
Geom Geom::makePolygon(const std::vector<Point2d>& pts)
{
    if (pts.size() < 3)
        return Geom();
    std::vector<Point2d> closed = pts;
    const Point2d& a = closed.front();
    const Point2d& b = closed.back();
    if (a.x != b.x || a.y != b.y)
        closed.push_back(a);
    return makeRingPolygon(closed);
}

Geom Geom::makeRingPolygon(const std::vector<Point2d>& pts)
{
    if (pts.size() < 4)
        return Geom();   // 闭合四边形最少 4 个坐标
    const GEOSContextHandle_t c = ctx();
    GEOSCoordSequence* s = GEOSCoordSeq_create_r(c, (unsigned)pts.size(), 2);
    if (!s)
        return Geom();
    for (size_t i = 0; i < pts.size(); ++i) {
        GEOSCoordSeq_setX_r(c, s, (unsigned)i, pts[i].x);
        GEOSCoordSeq_setY_r(c, s, (unsigned)i, pts[i].y);
    }
    GEOSGeometry* ring = GEOSGeom_createLinearRing_r(c, s);
    if (!ring) {
        GEOSCoordSeq_destroy_r(c, s);
        return Geom();
    }
    GEOSGeometry* g = GEOSGeom_createPolygon_r(c, ring, nullptr, 0);
    if (!g) {
        GEOSGeom_destroy_r(c, ring);
        return Geom();
    }
    return Geom(g);
}

Geom Geom::parseWKT(const std::string& wkt)
{
    const GEOSContextHandle_t c = ctx();
    GEOSWKTReader* r = GEOSWKTReader_create_r(c);
    GEOSGeometry* g = GEOSWKTReader_read_r(c, r, wkt.c_str());
    GEOSWKTReader_destroy_r(c, r);
    return Geom(g);
}

// ---------------- 曲线离散化 ----------------
// 分段数策略：segments > 0 强制指定；否则按弦高误差自适应
//   θ = 2·acos(1 - e/r)，N = ceil(2π/θ)；下限 8（低于八边形失去圆的意义），
//   上限 4096（防极端误差要求撑爆 GEOS 内存）。

static int curveSegments(double featureRadius, double maxError, int segments)
{
    constexpr int kMinSeg = 8;
    constexpr int kMaxSeg = 4096;
    if (segments > 0)
        return std::clamp(segments, kMinSeg, kMaxSeg);
    if (featureRadius <= 0 || maxError <= 0)
        return kMaxSeg;
    const double ratio = 1.0 - maxError / featureRadius;
    if (ratio <= -1.0 || ratio >= 1.0)
        return kMaxSeg;
    const double theta = 2.0 * std::acos(ratio);
    if (theta <= 0)
        return kMaxSeg;
    const int n = (int)std::ceil(2.0 * M_PI / theta);
    return std::clamp(n, kMinSeg, kMaxSeg);
}

Geom Geom::makeCircle(double cx, double cy, double r, double maxError, int segments)
{
    if (r <= 0)
        return Geom();
    const int n = curveSegments(r, maxError, segments);
    std::vector<Point2d> pts;
    pts.reserve((size_t)n + 1);
    for (int i = 0; i < n; ++i) {
        const double t = 2.0 * M_PI * (double)i / (double)n;
        pts.push_back(Point2d{ cx + r * std::cos(t), cy + r * std::sin(t) });
    }
    pts.push_back(pts.front());   // 闭合
    return makeRingPolygon(pts);
}

// 椭圆：中心 (xc,yc)，半轴 a/b，长轴方向 thetaDeg（度）。离散精度按长半轴算（最坏曲率）。
Geom Geom::makeEllipse(double xc, double yc, double a, double b, double thetaDeg,
                       double maxError, int segments)
{
    if (a <= 0 || b <= 0)
        return Geom();
    const int n = curveSegments(std::max(a, b), maxError, segments);
    const double th = thetaDeg * M_PI / 180.0;
    const double ct = std::cos(th), st = std::sin(th);
    std::vector<Point2d> pts;
    pts.reserve((size_t)n + 1);
    for (int i = 0; i < n; ++i) {
        const double t = 2.0 * M_PI * (double)i / (double)n;
        const double ex = a * std::cos(t);
        const double ey = b * std::sin(t);
        pts.push_back(Point2d{ xc + ex * ct - ey * st,
                               yc + ex * st + ey * ct });
    }
    pts.push_back(pts.front());   // 闭合
    return makeRingPolygon(pts);
}

// ---------------- 公共工厂转发（非模板部分） ----------------

Geom Geom::createPoint(double x, double y) { return makePoint(x, y); }
Geom Geom::fromWKT(const std::string& wkt) { return parseWKT(wkt); }
Geom Geom::createCircle(double cx, double cy, double r, double max_error, int segments)
{
    return makeCircle(cx, cy, r, max_error, segments);
}
Geom Geom::createEllipse(double xc, double yc, double a, double b, double theta_deg,
                         double max_error, int segments)
{
    return makeEllipse(xc, yc, a, b, theta_deg, max_error, segments);
}

} // namespace shapely
