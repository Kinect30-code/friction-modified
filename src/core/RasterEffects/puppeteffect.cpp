/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# See 'README.md' for more information.
#
*/

#include "puppeteffect.h"

#include "Boxes/boundingbox.h"
#include "Boxes/boxrenderdata.h"
#include "MovablePoints/animatedpoint.h"
#include "GUI/propertynamedialog.h"
#include "typemenu.h"
#include "appsupport.h"

#include <array>
#include <limits>
#include <QHash>
#include <QtMath>

namespace {

class PuppetPinPoint : public AnimatedPoint {
public:
    explicit PuppetPinPoint(PuppetPin* const pin) :
        AnimatedPoint(pin->positionAnimator(), TYPE_PATH_POINT),
        mPin(pin) {}

    QPointF getRelativePos() const override;
    void setRelativePos(const QPointF &relPos) override;
    bool isVisible(const CanvasMode mode) const override;
private:
    PuppetPin* const mPin;
};

struct PuppetHandle {
    QPointF from;
    QPointF to;
};

struct PuppetMeshSettings {
    int density = 18;
    qreal expansion = 0.0;
    qreal pinStiffness = 0.7;
};

struct PuppetMeshData {
    struct Triangle {
        int a = -1;
        int b = -1;
        int c = -1;
    };

    int width = 0;
    int height = 0;
    QVector<QPointF> sourceVerts;
    QVector<QPointF> deformedVerts;
    QVector<uchar> boundaryVerts;
    QVector<Triangle> triangles;

    bool hasAnyActiveCells() const {
        return !triangles.isEmpty();
    }
};

struct PreviewMeshCacheEntry {
    PuppetMeshData mesh;
};

static QHash<const PuppetEffect*, PreviewMeshCacheEntry> gPreviewMeshCache;

static void applyPinDeformation(PuppetMeshData& mesh,
                                const QVector<PuppetHandle>& pinHandles,
                                const PuppetMeshSettings& settings) {
    mesh.deformedVerts.resize(mesh.sourceVerts.size());
    if(pinHandles.isEmpty()) {
        mesh.deformedVerts = mesh.sourceVerts;
        return;
    }

    const qreal expansionPx = qMax<qreal>(0.0, settings.expansion);
    const qreal stiffness = qBound<qreal>(0.05, settings.pinStiffness, 1.0);
    const qreal alphaDiag = qSqrt(qreal(mesh.width)*mesh.width +
                                  qreal(mesh.height)*mesh.height);
    const qreal supportRadius = qMax<qreal>(12.0 + expansionPx,
                                            alphaDiag*(0.10 + (1.0 - stiffness)*0.28));
    const qreal supportRadius2 = supportRadius*supportRadius;
    const qreal hardRadius = supportRadius*(0.12 + stiffness*0.28);
    const qreal hardRadius2 = hardRadius*hardRadius;
    const qreal falloffPower = 2.0 + stiffness*6.0;

    for(int vertIndex = 0; vertIndex < mesh.sourceVerts.size(); ++vertIndex) {
        const QPointF sourcePt = mesh.sourceVerts.at(vertIndex);
        struct LocalInfluence {
            QPointF delta;
            qreal weight = 0.0;
        };
        QVector<LocalInfluence> localInfluences;
        localInfluences.reserve(pinHandles.size());
        qreal maxWeight = 0.0;

        for(const auto& handle : pinHandles) {
            const QPointF sourceHandle(handle.from.x()*(mesh.width - 1.0),
                                       handle.from.y()*(mesh.height - 1.0));
            const QPointF currentHandle(handle.to.x()*(mesh.width - 1.0),
                                        handle.to.y()*(mesh.height - 1.0));
            const QPointF diff = sourcePt - sourceHandle;
            const qreal dist2 = diff.x()*diff.x() + diff.y()*diff.y();
            if(dist2 >= supportRadius2) {
                continue;
            }

            qreal influence = 1.0;
            if(dist2 > hardRadius2) {
                const qreal normalizedDist = qBound<qreal>(
                            0.0,
                            (dist2 - hardRadius2)/qMax<qreal>(1e-6, supportRadius2 - hardRadius2),
                            1.0);
                influence = qPow(1.0 - normalizedDist, falloffPower);
            }
            maxWeight = qMax(maxWeight, influence);
            localInfluences.append({currentHandle - sourceHandle, influence});
        }

        if(localInfluences.isEmpty()) {
            mesh.deformedVerts[vertIndex] = sourcePt;
            continue;
        }

        const qreal blendThreshold = qMax<qreal>(0.12, 0.32 - stiffness*0.18);
        QPointF blendedDelta(0, 0);
        qreal totalWeight = 0.0;
        for(const auto& local : localInfluences) {
            if(local.weight < maxWeight*blendThreshold) {
                continue;
            }
            blendedDelta += local.delta*local.weight;
            totalWeight += local.weight;
        }

        if(totalWeight <= 1e-6) {
            mesh.deformedVerts[vertIndex] = sourcePt;
        } else {
            mesh.deformedVerts[vertIndex] = sourcePt + blendedDelta/totalWeight;
        }
    }
}

static QPointF normalizedToCanvas(const BoundingBox* const owner,
                                  const QPointF& normalized) {
    qreal left = 0;
    qreal top = 0;
    qreal width = 100;
    qreal height = 100;
    if(owner) {
        const auto rect = owner->getRelBoundingRect();
        left = rect.left();
        top = rect.top();
        width = qMax<qreal>(1., rect.width());
        height = qMax<qreal>(1., rect.height());
    }
    return {left + normalized.x()*width, top + normalized.y()*height};
}

static QPointF canvasToNormalized(const BoundingBox* const owner,
                                  const QPointF& point) {
    qreal left = 0;
    qreal top = 0;
    qreal width = 100;
    qreal height = 100;
    if(owner) {
        const auto rect = owner->getRelBoundingRect();
        left = rect.left();
        top = rect.top();
        width = qMax<qreal>(1., rect.width());
        height = qMax<qreal>(1., rect.height());
    }
    return {(point.x() - left)/width, (point.y() - top)/height};
}

static QVector<PuppetHandle> collectWarpHandles(
        const PuppetDeformAnimator* const deform,
        const qreal relFrame,
        const qreal influence,
        const bool inverse = false) {
    QVector<PuppetHandle> handles;
    handles.reserve(deform ? deform->ca_getNumberOfChildren() : 0);

    if(!deform) {
        return handles;
    }

    const int count = deform->ca_getNumberOfChildren();
    for(int i = 0; i < count; ++i) {
        const auto* const pin = deform->getChild(i);
        if(!pin) {
            continue;
        }
        const QPointF source = pin->normalizedSource();
        const QPointF current = pin->normalizedPosition(relFrame);
        const QPointF blended = source + (current - source)*influence;
        handles.append(inverse ? PuppetHandle{blended, source}
                               : PuppetHandle{source, blended});
    }
    return handles;
}

static QPointF normalizedToImage(const QPointF& normalized,
                                 const int width,
                                 const int height) {
    return {normalized.x()*(width - 1.0), normalized.y()*(height - 1.0)};
}

static QPointF imageToLocalRect(const QRectF& rect,
                                const QPointF& imagePos,
                                const int width,
                                const int height) {
    const qreal denomW = qMax(1, width - 1);
    const qreal denomH = qMax(1, height - 1);
    return {
        rect.left() + imagePos.x()*rect.width()/denomW,
        rect.top() + imagePos.y()*rect.height()/denomH
    };
}

template <typename AlphaSampler>
static QPointF projectToOpaquePixel(const QPointF& preferred,
                                    const int width,
                                    const int height,
                                    const AlphaSampler& sampleAlpha,
                                    const int maxRadius) {
    const int px = qBound(0, qRound(preferred.x()), width - 1);
    const int py = qBound(0, qRound(preferred.y()), height - 1);
    if(sampleAlpha(px, py) > 8) {
        return QPointF(px, py);
    }

    qreal bestDist2 = std::numeric_limits<qreal>::max();
    QPointF best = preferred;
    bool found = false;
    for(int radius = 1; radius <= maxRadius; ++radius) {
        const int minX = qMax(0, px - radius);
        const int maxX = qMin(width - 1, px + radius);
        const int minY = qMax(0, py - radius);
        const int maxY = qMin(height - 1, py + radius);
        for(int y = minY; y <= maxY; ++y) {
            for(int x = minX; x <= maxX; ++x) {
                if(x != minX && x != maxX && y != minY && y != maxY) {
                    continue;
                }
                if(sampleAlpha(x, y) <= 8) {
                    continue;
                }
                const QPointF candidate(x, y);
                const QPointF diff = candidate - preferred;
                const qreal dist2 = diff.x()*diff.x() + diff.y()*diff.y();
                if(dist2 < bestDist2) {
                    bestDist2 = dist2;
                    best = candidate;
                    found = true;
                }
            }
        }
        if(found) {
            return best;
        }
    }
    return preferred;
}

template <typename AlphaSampler>
static PuppetMeshData buildActiveMesh(const int width,
                                      const int height,
                                      const AlphaSampler& sampleAlpha,
                                      const QVector<PuppetHandle>& pinHandles,
                                      const PuppetMeshSettings& settings) {
    PuppetMeshData mesh;
    mesh.width = width;
    mesh.height = height;

    int alphaMinX = width;
    int alphaMinY = height;
    int alphaMaxX = -1;
    int alphaMaxY = -1;
    for(int y = 0; y < height; ++y) {
        for(int x = 0; x < width; ++x) {
            if(sampleAlpha(x, y) <= 8) {
                continue;
            }
            alphaMinX = qMin(alphaMinX, x);
            alphaMinY = qMin(alphaMinY, y);
            alphaMaxX = qMax(alphaMaxX, x);
            alphaMaxY = qMax(alphaMaxY, y);
        }
    }

    if(alphaMaxX < alphaMinX || alphaMaxY < alphaMinY) {
        return mesh;
    }

    const qreal alphaWidth = qMax(1, alphaMaxX - alphaMinX + 1);
    const qreal alphaHeight = qMax(1, alphaMaxY - alphaMinY + 1);
    const qreal alphaDiag = qSqrt(alphaWidth*alphaWidth + alphaHeight*alphaHeight);
    const int density = qBound(6, settings.density, 40);
    const int seedCols = qMax(6, density);
    const int seedRows = qMax(6, qRound(density*alphaHeight/qMax<qreal>(1.0, alphaWidth)));
    const qreal expansionPx = qMax<qreal>(0.0, settings.expansion);
    const qreal boundaryStep = qMax<qreal>(3.0, alphaDiag/(density*1.25));
    const qreal interiorStepX = qMax<qreal>(4.0, alphaWidth/seedCols);
    const qreal interiorStepY = qMax<qreal>(4.0, alphaHeight/seedRows);
    const qreal pinLocalStep = qMax<qreal>(4.0, qMin(interiorStepX, interiorStepY)*0.7);
    const int searchRadius = qMax(6, qRound(qMax(width, height)/qreal(seedCols*3)));
    const int meshMinX = qBound(0, qFloor(alphaMinX - expansionPx), width - 1);
    const int meshMinY = qBound(0, qFloor(alphaMinY - expansionPx), height - 1);
    const int meshMaxX = qBound(0, qCeil(alphaMaxX + expansionPx), width - 1);
    const int meshMaxY = qBound(0, qCeil(alphaMaxY + expansionPx), height - 1);

    auto alphaAtPoint = [&](const QPointF& pt) {
        return sampleAlpha(qRound(pt.x()), qRound(pt.y()));
    };

    auto pointOpaque = [&](const QPointF& pt) {
        return alphaAtPoint(pt) > 8;
    };

    auto pointInsideExpandedMesh = [&](const QPointF& pt) {
        if(pointOpaque(pt)) {
            return true;
        }
        if(expansionPx <= 0.0) {
            return false;
        }
        const QPointF nearestOpaque = projectToOpaquePixel(pt, width, height,
                                                           sampleAlpha, qCeil(expansionPx) + searchRadius);
        if(!pointOpaque(nearestOpaque)) {
            return false;
        }
        return QLineF(pt, nearestOpaque).length() <= expansionPx + 1.0;
    };

    auto tryAppendPoint = [&](const QPointF& candidate, const bool boundary) {
        QPointF meshPoint = candidate;
        if(pointOpaque(candidate)) {
            meshPoint = projectToOpaquePixel(candidate, width, height,
                                             sampleAlpha, searchRadius);
        } else if(!pointInsideExpandedMesh(candidate)) {
            return;
        }
        const qreal minDist2 = qPow(boundary ? boundaryStep*0.4 : qMin(interiorStepX, interiorStepY)*0.45, 2.0);
        for(int i = 0; i < mesh.sourceVerts.size(); ++i) {
            const QPointF diff = mesh.sourceVerts.at(i) - meshPoint;
            const qreal dist2 = diff.x()*diff.x() + diff.y()*diff.y();
            if(dist2 <= minDist2) {
                if(boundary) {
                    mesh.boundaryVerts[i] = 1;
                }
                return;
            }
        }
        mesh.sourceVerts.append(meshPoint);
        mesh.boundaryVerts.append(boundary ? 1 : 0);
    };

    const int contourStride = qMax(1, qFloor(boundaryStep*0.5));
    for(int y = meshMinY; y <= meshMaxY; y += contourStride) {
        for(int x = meshMinX; x < meshMaxX; x += contourStride) {
            const bool opaqueA = sampleAlpha(x, y) > 8;
            const bool opaqueB = sampleAlpha(qMin(meshMaxX, x + contourStride), y) > 8;
            if(opaqueA != opaqueB) {
                tryAppendPoint(QPointF(x + contourStride*0.5, y), true);
            }
        }
    }

    for(int x = meshMinX; x <= meshMaxX; x += contourStride) {
        for(int y = meshMinY; y < meshMaxY; y += contourStride) {
            const bool opaqueA = sampleAlpha(x, y) > 8;
            const bool opaqueB = sampleAlpha(x, qMin(meshMaxY, y + contourStride)) > 8;
            if(opaqueA != opaqueB) {
                tryAppendPoint(QPointF(x, y + contourStride*0.5), true);
            }
        }
    }

    for(qreal y = meshMinY; y <= meshMaxY; y += boundaryStep) {
        const int yi = qBound(meshMinY, qRound(y), meshMaxY);
        int first = -1;
        int last = -1;
        for(int x = meshMinX; x <= meshMaxX; ++x) {
            if(sampleAlpha(x, yi) > 8) {
                if(first < 0) {
                    first = x;
                }
                last = x;
            }
        }
        if(first >= 0) {
            tryAppendPoint(QPointF(first, yi), true);
            tryAppendPoint(QPointF(last, yi), true);
        }
    }

    for(qreal x = meshMinX; x <= meshMaxX; x += boundaryStep) {
        const int xi = qBound(meshMinX, qRound(x), meshMaxX);
        int first = -1;
        int last = -1;
        for(int y = meshMinY; y <= meshMaxY; ++y) {
            if(sampleAlpha(xi, y) > 8) {
                if(first < 0) {
                    first = y;
                }
                last = y;
            }
        }
        if(first >= 0) {
            tryAppendPoint(QPointF(xi, first), true);
            tryAppendPoint(QPointF(xi, last), true);
        }
    }

    for(int row = 0; row <= seedRows; ++row) {
        const qreal y = meshMinY + qreal(row)*(meshMaxY - meshMinY)/qMax(1, seedRows);
        const qreal xOffset = (row % 2 == 0) ? 0.0 : interiorStepX*0.5;
        for(qreal x = meshMinX + xOffset; x <= meshMaxX; x += interiorStepX) {
            tryAppendPoint(QPointF(x, y), false);
        }
    }

    for(const auto& handle : pinHandles) {
        const QPointF pinPos = normalizedToImage(handle.from, width, height);
        tryAppendPoint(pinPos, false);
        const qreal localRadius = qMax<qreal>(pinLocalStep*2.5, expansionPx);
        const int ringCount = qMax(2, qCeil(localRadius/qMax<qreal>(1.0, pinLocalStep)));
        for(int ring = 1; ring <= ringCount; ++ring) {
            const qreal radius = pinLocalStep*ring;
            const int angleSteps = 8 + ring*4;
            for(int step = 0; step < angleSteps; ++step) {
                const qreal angle = step*(2.0*M_PI/angleSteps);
                tryAppendPoint(pinPos + QPointF(qCos(angle)*radius,
                                                qSin(angle)*radius),
                               false);
            }
        }

        for(qreal localY = -localRadius; localY <= localRadius; localY += pinLocalStep) {
            const bool offsetRow = (qRound(localY/pinLocalStep) & 1) != 0;
            for(qreal localX = -localRadius; localX <= localRadius; localX += pinLocalStep) {
                QPointF offset(localX + (offsetRow ? pinLocalStep*0.5 : 0.0), localY);
                if(QLineF(QPointF(0, 0), offset).length() <= localRadius) {
                    tryAppendPoint(pinPos + offset, false);
                }
            }
        }
    }

    for(int i = 0; i < pinHandles.size(); ++i) {
        const QPointF pinA = normalizedToImage(pinHandles.at(i).from, width, height);
        for(int j = i + 1; j < pinHandles.size(); ++j) {
            const QPointF pinB = normalizedToImage(pinHandles.at(j).from, width, height);
            const qreal dist = QLineF(pinA, pinB).length();
            if(dist <= qMax<qreal>(expansionPx*2.0, pinLocalStep*8.0)) {
                tryAppendPoint((pinA + pinB)*0.5, false);
                tryAppendPoint(pinA*0.67 + pinB*0.33, false);
                tryAppendPoint(pinA*0.33 + pinB*0.67, false);
            }
        }
    }

    if(mesh.sourceVerts.size() < 3) {
        mesh.sourceVerts.clear();
        mesh.boundaryVerts.clear();
        return mesh;
    }

    struct EdgeKey {
        int a = -1;
        int b = -1;

        bool operator==(const EdgeKey& other) const {
            return a == other.a && b == other.b;
        }
    };

    auto makeEdge = [](const int i0, const int i1) {
        return EdgeKey{qMin(i0, i1), qMax(i0, i1)};
    };

    auto triangleArea2 = [&](const int ia, const int ib, const int ic,
                             const QVector<QPointF>& verts) {
        const QPointF& a = verts.at(ia);
        const QPointF& b = verts.at(ib);
        const QPointF& c = verts.at(ic);
        return (b.x() - a.x())*(c.y() - a.y()) - (b.y() - a.y())*(c.x() - a.x());
    };

    auto pointInCircumcircle = [&](const QPointF& p,
                                   const PuppetMeshData::Triangle& tri,
                                   const QVector<QPointF>& verts) {
        const QPointF a = verts.at(tri.a);
        const QPointF b = verts.at(tri.b);
        const QPointF c = verts.at(tri.c);
        const qreal ax = a.x() - p.x();
        const qreal ay = a.y() - p.y();
        const qreal bx = b.x() - p.x();
        const qreal by = b.y() - p.y();
        const qreal cx = c.x() - p.x();
        const qreal cy = c.y() - p.y();
        const qreal det = (ax*ax + ay*ay)*(bx*cy - by*cx) -
                          (bx*bx + by*by)*(ax*cy - ay*cx) +
                          (cx*cx + cy*cy)*(ax*by - ay*bx);
        const qreal orient = triangleArea2(tri.a, tri.b, tri.c, verts);
        return orient > 0 ? det > 1e-4 : det < -1e-4;
    };

    QVector<QPointF> triangulationVerts = mesh.sourceVerts;
    const int sourceVertCount = triangulationVerts.size();
    const qreal superPad = qMax(alphaWidth, alphaHeight)*4.0 + 32.0;
    triangulationVerts.append(QPointF(alphaMinX - superPad, alphaMinY - superPad));
    triangulationVerts.append(QPointF(alphaMaxX + superPad, alphaMinY - superPad));
    triangulationVerts.append(QPointF((alphaMinX + alphaMaxX)*0.5, alphaMaxY + superPad));
    const int superA = sourceVertCount;
    const int superB = sourceVertCount + 1;
    const int superC = sourceVertCount + 2;

    QVector<PuppetMeshData::Triangle> triangles;
    triangles.append({superA, superB, superC});

    for(int pointIndex = 0; pointIndex < sourceVertCount; ++pointIndex) {
        const QPointF point = triangulationVerts.at(pointIndex);
        QVector<int> badTriangles;
        for(int triIndex = 0; triIndex < triangles.size(); ++triIndex) {
            if(pointInCircumcircle(point, triangles.at(triIndex), triangulationVerts)) {
                badTriangles.append(triIndex);
            }
        }

        QVector<EdgeKey> polygon;
        for(const int triIndex : badTriangles) {
            const auto& tri = triangles.at(triIndex);
            const std::array<EdgeKey, 3> triEdges = {
                makeEdge(tri.a, tri.b),
                makeEdge(tri.b, tri.c),
                makeEdge(tri.c, tri.a)
            };
            for(const auto& edge : triEdges) {
                bool removed = false;
                for(int i = 0; i < polygon.size(); ++i) {
                    if(polygon.at(i) == edge) {
                        polygon.removeAt(i);
                        removed = true;
                        break;
                    }
                }
                if(!removed) {
                    polygon.append(edge);
                }
            }
        }

        std::sort(badTriangles.begin(), badTriangles.end(), std::greater<int>());
        for(const int triIndex : badTriangles) {
            triangles.removeAt(triIndex);
        }

        for(const auto& edge : polygon) {
            PuppetMeshData::Triangle tri{edge.a, edge.b, pointIndex};
            if(qAbs(triangleArea2(tri.a, tri.b, tri.c, triangulationVerts)) > 1e-4) {
                triangles.append(tri);
            }
        }
    }

    auto triangleInsideOpaque = [&](const PuppetMeshData::Triangle& tri) {
        if(tri.a >= sourceVertCount || tri.b >= sourceVertCount || tri.c >= sourceVertCount) {
            return false;
        }
        const QPointF& a = mesh.sourceVerts.at(tri.a);
        const QPointF& b = mesh.sourceVerts.at(tri.b);
        const QPointF& c = mesh.sourceVerts.at(tri.c);
        const QPointF centroid = (a + b + c)/3.0;
        const QPointF ab = (a + b)*0.5;
        const QPointF bc = (b + c)*0.5;
        const QPointF ca = (c + a)*0.5;
        int votes = 0;
        votes += pointInsideExpandedMesh(centroid) ? 1 : 0;
        votes += pointInsideExpandedMesh(ab) ? 1 : 0;
        votes += pointInsideExpandedMesh(bc) ? 1 : 0;
        votes += pointInsideExpandedMesh(ca) ? 1 : 0;
        return votes >= 2;
    };

    for(const auto& tri : triangles) {
        if(triangleInsideOpaque(tri)) {
            mesh.triangles.append(tri);
        }
    }

    applyPinDeformation(mesh, pinHandles, settings);
    return mesh;
}

static bool buildActiveMeshFromImage(const sk_sp<SkImage>& image,
                                     const QVector<PuppetHandle>& pinHandles,
                                     const PuppetMeshSettings& settings,
                                     PuppetMeshData& mesh) {
    if(!image) {
        return false;
    }
    const auto raster = image->makeRasterImage();
    SkPixmap pixmap;
    if(!raster || !raster->peekPixels(&pixmap)) {
        return false;
    }

    SkBitmap bitmap;
    bitmap.installPixels(pixmap);
    const int width = bitmap.width();
    const int height = bitmap.height();
    if(width <= 1 || height <= 1) {
        return false;
    }

    auto sampleAlpha = [&bitmap, width, height](const int x, const int y) {
        const int sx = qBound(0, x, width - 1);
        const int sy = qBound(0, y, height - 1);
        const auto *px = static_cast<const uchar*>(bitmap.getAddr(sx, sy));
        return int(px[3]);
    };

    mesh = buildActiveMesh(width, height, sampleAlpha, pinHandles, settings);
    return mesh.hasAnyActiveCells();
}

class PuppetCaller : public RasterEffectCaller {
public:
    PuppetCaller(const QVector<PuppetHandle>& pinHandles,
                 const PuppetMeshSettings& settings,
                 const QMargins& margins) :
        RasterEffectCaller(HardwareSupport::cpuOnly, true, margins),
        mPinHandles(pinHandles),
        mSettings(settings) {}

    void processCpu(CpuRenderTools& renderTools,
                    const CpuRenderData& data) override {
        const auto& src = renderTools.fSrcBtmp;
        auto& dst = renderTools.fDstBtmp;
        SkCanvas clearCanvas(dst);
        clearCanvas.clear(SK_ColorTRANSPARENT);

        const int width = src.width();
        const int height = src.height();

        if(width <= 1 || height <= 1) {
            return;
        }

        auto sampleAlpha = [&src, width, height](const int x, const int y) {
            const int sx = qBound(0, x, width - 1);
            const int sy = qBound(0, y, height - 1);
            const auto *px = static_cast<const uchar*>(src.getAddr(sx, sy));
            return int(px[3]);
        };
        const auto mesh = buildActiveMesh(width, height, sampleAlpha, mPinHandles, mSettings);
        if(!mesh.hasAnyActiveCells()) {
            return;
        }

        auto sampleBilinear = [&src, width, height](const QPointF& srcPt, uchar *dstPx) {
            if(srcPt.x() < 0.0 || srcPt.y() < 0.0 ||
               srcPt.x() > width - 1.0 || srcPt.y() > height - 1.0) {
                dstPx[0] = 0;
                dstPx[1] = 0;
                dstPx[2] = 0;
                dstPx[3] = 0;
                return;
            }

            const int sx0 = qBound(0, qFloor(srcPt.x()), width - 1);
            const int sy0 = qBound(0, qFloor(srcPt.y()), height - 1);
            const int sx1 = qMin(width - 1, sx0 + 1);
            const int sy1 = qMin(height - 1, sy0 + 1);
            const qreal tx = qBound<qreal>(0., srcPt.x() - sx0, 1.);
            const qreal ty = qBound<qreal>(0., srcPt.y() - sy0, 1.);

            const auto *p00 = static_cast<const uchar*>(src.getAddr(sx0, sy0));
            const auto *p10 = static_cast<const uchar*>(src.getAddr(sx1, sy0));
            const auto *p01 = static_cast<const uchar*>(src.getAddr(sx0, sy1));
            const auto *p11 = static_cast<const uchar*>(src.getAddr(sx1, sy1));

            const auto mix = [tx, ty](const qreal a00, const qreal a10,
                                      const qreal a01, const qreal a11) {
                const qreal m0 = a00*(1. - tx) + a10*tx;
                const qreal m1 = a01*(1. - tx) + a11*tx;
                return m0*(1. - ty) + m1*ty;
            };

            dstPx[0] = qBound(0, qRound(mix(p00[0], p10[0], p01[0], p11[0])), 255);
            dstPx[1] = qBound(0, qRound(mix(p00[1], p10[1], p01[1], p11[1])), 255);
            dstPx[2] = qBound(0, qRound(mix(p00[2], p10[2], p01[2], p11[2])), 255);
            dstPx[3] = qBound(0, qRound(mix(p00[3], p10[3], p01[3], p11[3])), 255);
        };

        auto rasterizeTriangle = [&](const QPointF& d0,
                                     const QPointF& d1,
                                     const QPointF& d2,
                                     const QPointF& s0,
                                     const QPointF& s1,
                                     const QPointF& s2) {
            const qreal denom = (d1.y() - d2.y())*(d0.x() - d2.x()) +
                                (d2.x() - d1.x())*(d0.y() - d2.y());
            if(qFuzzyIsNull(denom)) {
                return;
            }

            const int xMin = qMax(data.fTexTile.left(), qFloor(qMin(d0.x(), qMin(d1.x(), d2.x()))));
            const int xMax = qMin(data.fTexTile.right(), qCeil(qMax(d0.x(), qMax(d1.x(), d2.x()))));
            const int yMin = qMax(data.fTexTile.top(), qFloor(qMin(d0.y(), qMin(d1.y(), d2.y()))));
            const int yMax = qMin(data.fTexTile.bottom(), qCeil(qMax(d0.y(), qMax(d1.y(), d2.y()))));

            for(int yi = yMin; yi < yMax; ++yi) {
                for(int xi = xMin; xi < xMax; ++xi) {
                    const QPointF p(xi + 0.5, yi + 0.5);
                    const qreal w0 = ((d1.y() - d2.y())*(p.x() - d2.x()) +
                                      (d2.x() - d1.x())*(p.y() - d2.y()))/denom;
                    const qreal w1 = ((d2.y() - d0.y())*(p.x() - d2.x()) +
                                      (d0.x() - d2.x())*(p.y() - d2.y()))/denom;
                    const qreal w2 = 1.0 - w0 - w1;
                    if(w0 < -1e-4 || w1 < -1e-4 || w2 < -1e-4) {
                        continue;
                    }

                    const QPointF srcPt = s0*w0 + s1*w1 + s2*w2;
                    auto *dstPx = static_cast<uchar*>(dst.getAddr(xi - data.fTexTile.left(),
                                                                  yi - data.fTexTile.top()));
                    sampleBilinear(srcPt, dstPx);
                }
            }
        };

        for(const auto& tri : mesh.triangles) {
            rasterizeTriangle(mesh.deformedVerts.at(tri.a),
                              mesh.deformedVerts.at(tri.b),
                              mesh.deformedVerts.at(tri.c),
                              mesh.sourceVerts.at(tri.a),
                              mesh.sourceVerts.at(tri.b),
                              mesh.sourceVerts.at(tri.c));
        }
    }
private:
    QVector<PuppetHandle> mPinHandles;
    PuppetMeshSettings mSettings;
};

static QMargins buildPuppetMargins(const QVector<PuppetHandle>& forwardHandles,
                                   const int width,
                                   const int height,
                                   const qreal expansionPx) {
    qreal left = 0;
    qreal right = 0;
    qreal top = 0;
    qreal bottom = 0;
    for(const auto& handle : forwardHandles) {
        const QPointF src = normalizedToImage(handle.from, width, height);
        const QPointF dst = normalizedToImage(handle.to, width, height);
        const QPointF delta = dst - src;
        left = qMax(left, -delta.x());
        right = qMax(right, delta.x());
        top = qMax(top, -delta.y());
        bottom = qMax(bottom, delta.y());
    }
    left += expansionPx;
    right += expansionPx;
    top += expansionPx;
    bottom += expansionPx;
    return {qCeil(left), qCeil(top), qCeil(right), qCeil(bottom)};
}

}

QPointF PuppetPinPoint::getRelativePos() const {
    return normalizedToCanvas(mPin->getFirstAncestor<BoundingBox>(),
                              AnimatedPoint::getRelativePos());
}

void PuppetPinPoint::setRelativePos(const QPointF &relPos) {
    AnimatedPoint::setRelativePos(
                canvasToNormalized(mPin->getFirstAncestor<BoundingBox>(),
                                   relPos));
}

bool PuppetPinPoint::isVisible(const CanvasMode mode) const {
    return mode == CanvasMode::pointTransform ||
           mode == CanvasMode::puppetPinCreate;
}

PuppetPin::PuppetPin() :
    StaticComplexAnimator(QStringLiteral("Pin 1")) {
    mPosition = enve::make_shared<QPointFAnimator>(
                QPointF(0.5, 0.5),
                QPointF(-4.0, -4.0),
                QPointF(4.0, 4.0),
                QPointF(0.001, 0.001),
                QStringLiteral("x"),
                QStringLiteral("y"),
                QStringLiteral("Position"));
    mSourcePosition = enve::make_shared<QPointFAnimator>(
                QPointF(0.5, 0.5),
                QPointF(-4.0, -4.0),
                QPointF(4.0, 4.0),
                QPointF(0.001, 0.001),
                QStringLiteral("x"),
                QStringLiteral("y"),
                QStringLiteral("Source Position"));

    mSourcePosition->SWT_hide();

    ca_addChild(mPosition);
    ca_addChild(mSourcePosition);
    ca_setGUIProperty(mPosition.get());

    setPointsHandler(enve::make_shared<PointsHandler>());
    getPointsHandler()->appendPt(enve::make_shared<PuppetPinPoint>(this));
}

void PuppetPin::prp_setupTreeViewMenu(PropertyMenu * const menu) {
    if(menu->hasActionsForType<PuppetPin>()) {
        return;
    }
    menu->addedActionsForType<PuppetPin>();
    const auto parentWidget = menu->getParentWidget();
    menu->addPlainAction(
                QIcon::fromTheme("dialog-information"),
                tr("Rename"),
                [this, parentWidget]() {
        PropertyNameDialog::sRenameProperty(this, parentWidget);
    });
    menu->addPlainAction(
                QIcon::fromTheme("trash"),
                tr("Delete Pin"),
                [this]() {
        if(auto* const parent = getParent<PuppetDeformAnimator>()) {
            auto* const effect = parent->getFirstAncestor<PuppetEffect>();
            parent->removeChild(ref<PuppetPin>());
            if(effect) {
                effect->notifyPinsChanged();
            }
        }
    });
    menu->addSeparator();
    StaticComplexAnimator::prp_setupTreeViewMenu(menu);
}

QPointF PuppetPin::normalizedSource() const {
    return mSourcePosition ? mSourcePosition->getBaseValue() : QPointF(0.5, 0.5);
}

QPointF PuppetPin::normalizedPosition(const qreal relFrame) const {
    return mPosition ? mPosition->getEffectiveValue(relFrame) : QPointF(0.5, 0.5);
}

QPointF PuppetPin::normalizedPosition() const {
    return mPosition ? mPosition->getEffectiveValue() : QPointF(0.5, 0.5);
}

void PuppetPin::setNormalizedSource(const QPointF &value) {
    if(mSourcePosition) {
        mSourcePosition->setBaseValue(value);
    }
}

void PuppetPin::setNormalizedPosition(const QPointF &value) {
    if(mPosition) {
        mPosition->setBaseValue(value);
    }
}

void PuppetPin::initializeAt(const QPointF &normalizedPos) {
    setNormalizedSource(normalizedPos);
    setNormalizedPosition(normalizedPos);
}

QPointFAnimator *PuppetPin::positionAnimator() const {
    return mPosition.get();
}

QPointFAnimator *PuppetPin::sourceAnimator() const {
    return mSourcePosition.get();
}

PuppetDeformAnimator::PuppetDeformAnimator() :
    PuppetPinCollectionBase(QStringLiteral("Deform")) {}

void PuppetDeformAnimator::prp_setupTreeViewMenu(PropertyMenu * const menu) {
    if(!menu->hasActionsForType<PuppetDeformAnimator>()) {
        menu->addedActionsForType<PuppetDeformAnimator>();
        menu->addPlainAction<PuppetDeformAnimator>(
                    QIcon::fromTheme("list-add"),
                    tr("Add Pin"),
                    [](PuppetDeformAnimator* const deform) {
            if(auto* const effect = deform->getFirstAncestor<PuppetEffect>()) {
                effect->addPinAtNormalized(effect->defaultNewPinPosition());
            }
        });
        menu->addSeparator();
    }
    PuppetPinCollectionBase::prp_setupTreeViewMenu(menu);
}

PuppetPin *PuppetDeformAnimator::addPinAtNormalized(const QPointF &normalizedPos) {
    auto pin = enve::make_shared<PuppetPin>();
    pin->prp_setName(makeNameUnique(QStringLiteral("Pin 1"), pin.get()));
    pin->initializeAt(normalizedPos);
    addChild(pin);
    return pin.get();
}

PuppetMeshAnimator::PuppetMeshAnimator() :
    StaticComplexAnimator(QStringLiteral("Mesh 1")) {
    mDeform = enve::make_shared<PuppetDeformAnimator>();
    ca_addChild(mDeform);
    ca_setGUIProperty(mDeform.get());
}

PuppetDeformAnimator *PuppetMeshAnimator::deform() const {
    return mDeform.get();
}

PuppetEffect::PuppetEffect() :
    RasterEffect(QStringLiteral("Puppet"),
                 AppSupport::getRasterEffectHardwareSupport(
                     "Puppet", HardwareSupport::cpuOnly),
                 false,
                 RasterEffectType::PUPPET) {
    mMeshDensity = enve::make_shared<IntAnimator>(18, 6, 40, 1, QStringLiteral("Mesh Density"));
    mExpansion = enve::make_shared<QrealAnimator>(24.0, 0.0, 256.0, 1.0, QStringLiteral("Expansion"));
    mPinStiffness = enve::make_shared<QrealAnimator>(0.7, 0.05, 1.0, 0.01, QStringLiteral("Pin Stiffness"));
    mMesh = enve::make_shared<PuppetMeshAnimator>();
    ca_addChild(mMeshDensity);
    ca_addChild(mExpansion);
    ca_addChild(mPinStiffness);
    ca_addChild(mMesh);
    ca_setGUIProperty(mMesh.get());
    prp_enabledDrawingOnCanvas();
}

PuppetEffect::~PuppetEffect() {
    gPreviewMeshCache.remove(this);
}

stdsptr<RasterEffectCaller> PuppetEffect::getEffectCaller(
        const qreal relFrame,
        const qreal resolution,
        const qreal influence,
        BoxRenderData * const data) const {
    Q_UNUSED(resolution)
    if(!data || isZero4Dec(influence)) {
        return nullptr;
    }

    auto* const deformAnim = deform();
    if(!deformAnim || deformAnim->ca_getNumberOfChildren() <= 0) {
        return nullptr;
    }

    const int width = qMax(1, data->fGlobalRect.width());
    const int height = qMax(1, data->fGlobalRect.height());
    const auto forwardHandles = collectWarpHandles(deformAnim, relFrame, influence, false);
    const PuppetMeshSettings settings{
        mMeshDensity ? mMeshDensity->getEffectiveIntValue(relFrame) : 18,
        mExpansion ? mExpansion->getEffectiveValue(relFrame)*resolution : 0.0,
        mPinStiffness ? mPinStiffness->getEffectiveValue(relFrame) : 0.7
    };
    return enve::make_shared<PuppetCaller>(
                forwardHandles, settings,
                buildPuppetMargins(forwardHandles, width, height, settings.expansion));
}

void PuppetEffect::prp_setupTreeViewMenu(PropertyMenu * const menu) {
    if(!menu->hasActionsForType<PuppetEffect>()) {
        menu->addedActionsForType<PuppetEffect>();
        menu->addPlainAction<PuppetEffect>(
                    QIcon::fromTheme("list-add"),
                    tr("Add Pin"),
                    [](PuppetEffect* const effect) {
            effect->addPinAtNormalized(effect->defaultNewPinPosition());
        });
        menu->addSeparator();
    }
    RasterEffect::prp_setupTreeViewMenu(menu);
}

void PuppetEffect::prp_drawCanvasControls(
        SkCanvas * const canvas,
        const CanvasMode mode,
        const float invScale,
        const bool ctrlPressed) {
    Q_UNUSED(ctrlPressed)
    if((mode == CanvasMode::pointTransform ||
        mode == CanvasMode::puppetPinCreate) && isVisible()) {
        auto* const deformAnim = deform();
        const auto forwardHandles = collectWarpHandles(
                    deformAnim, anim_getCurrentRelFrame(), 1.0, false);
        QVector<QPointF> sourcePins;
        sourcePins.reserve(forwardHandles.size());
        for(const auto& handle : forwardHandles) {
            sourcePins.append(handle.from);
        }
        const PuppetMeshSettings settings{
            mMeshDensity ? mMeshDensity->getEffectiveIntValue() : 18,
            mExpansion ? mExpansion->getEffectiveValue() : 0.0,
            mPinStiffness ? mPinStiffness->getEffectiveValue() : 0.7
        };
        const auto* const owner = getFirstAncestor<BoundingBox>();
        PuppetMeshData mesh;
        bool hasMesh = false;
        if(owner) {
            const auto renderData = owner->getCurrentRenderData(anim_getCurrentRelFrame());
            if(renderData) {
                const auto image = renderData->requestImageCopy();
                const bool shouldRebuildPreview =
                        mPreviewCache.width != (image ? image->width() : 0) ||
                        mPreviewCache.height != (image ? image->height() : 0) ||
                        mPreviewCache.density != settings.density ||
                        !qFuzzyCompare(1.0 + mPreviewCache.expansion, 1.0 + settings.expansion) ||
                        mPreviewCache.sourcePins != sourcePins;
                if(shouldRebuildPreview) {
                    hasMesh = buildActiveMeshFromImage(image,
                                                       forwardHandles,
                                                       settings,
                                                       mesh);
                    if(hasMesh) {
                        mPreviewCache.width = mesh.width;
                        mPreviewCache.height = mesh.height;
                        mPreviewCache.density = settings.density;
                        mPreviewCache.expansion = settings.expansion;
                        mPreviewCache.sourcePins = sourcePins;
                        gPreviewMeshCache[this].mesh = mesh;
                    }
                } else {
                    const auto it = gPreviewMeshCache.constFind(this);
                    if(it != gPreviewMeshCache.constEnd()) {
                        mesh = it->mesh;
                        hasMesh = mesh.hasAnyActiveCells();
                        if(hasMesh) {
                            applyPinDeformation(mesh, forwardHandles, settings);
                        }
                    }
                }
            }
            if(hasMesh) {
                SkPaint paint;
                paint.setAntiAlias(true);
                paint.setStyle(SkPaint::kStroke_Style);
                paint.setStrokeWidth(qMax(1.f, invScale));
                paint.setColor(SkColorSetARGB(180, 255, 220, 80));

                const QRectF previewRect = renderData->fRelBoundingRect;
                const QMatrix previewTransform = renderData->fTotalTransform;
                const auto toCanvasPoint = [&](const QPointF& imagePt) {
                    const QPointF localPt =
                            imageToLocalRect(previewRect, imagePt, mesh.width, mesh.height);
                    return previewTransform.map(localPt);
                };

                auto drawEdge = [&](const QPointF& a, const QPointF& b) {
                    SkPath path;
                    path.moveTo(a.x(), a.y());
                    path.lineTo(b.x(), b.y());
                    canvas->drawPath(path, paint);
                };

                for(const auto& tri : mesh.triangles) {
                    const QPointF p0 = toCanvasPoint(mesh.deformedVerts.at(tri.a));
                    const QPointF p1 = toCanvasPoint(mesh.deformedVerts.at(tri.b));
                    const QPointF p2 = toCanvasPoint(mesh.deformedVerts.at(tri.c));
                    drawEdge(p0, p1);
                    drawEdge(p1, p2);
                    drawEdge(p2, p0);
                }
            }
        }
    }
    RasterEffect::prp_drawCanvasControls(canvas, mode, invScale, ctrlPressed);
}

PuppetPin *PuppetEffect::addPinAtNormalized(const QPointF &normalizedPos) {
    auto* const deformAnim = deform();
    if(!deformAnim) {
        return nullptr;
    }
    auto* const pin = deformAnim->addPinAtNormalized(normalizedPos);
    notifyPinsChanged();
    return pin;
}

QPointF PuppetEffect::defaultNewPinPosition() const {
    auto* const deformAnim = deform();
    const int count = deformAnim ? deformAnim->ca_getNumberOfChildren() : 0;
    if(count <= 0) {
        return {0.5, 0.5};
    }
    const qreal angle = count*1.2566370614;
    const qreal radius = qMin<qreal>(0.28, 0.08 + count*0.03);
    return {
        qBound<qreal>(0.1, 0.5 + qCos(angle)*radius, 0.9),
        qBound<qreal>(0.1, 0.5 + qSin(angle)*radius, 0.9)
    };
}

PuppetDeformAnimator *PuppetEffect::deform() const {
    return mMesh ? mMesh->deform() : nullptr;
}

void PuppetEffect::notifyPinsChanged() {
    gPreviewMeshCache.remove(this);
    if(auto* const owner = getFirstAncestor<BoundingBox>()) {
        owner->refreshCanvasControls();
        owner->prp_afterWholeInfluenceRangeChanged();
    }
}
