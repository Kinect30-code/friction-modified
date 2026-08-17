/*
 * AEP → friction mapping layer.
 *
 * Takes the model-independent parsed AEP project (ae_project.hpp) and
 * builds friction's BoundingBox/Canvas scene graph:
 *   - Composition      → Canvas (friction "precomp")
 *   - Precomp layer    → InternalLinkCanvas (createLink)
 *   - Shape layer      → SmartVectorPath / RectangleBox / Circle
 *   - Asset layer      → ImageBox (footage extracted to temp files)
 *   - Transform props  → BoxTransformAnimator (+ keyframes)
 *   - Blend mode       → setBlendModeSk
 *
 * Compiled as C++17 in the friction_aep_mapper library; only talks to
 * friction core through public headers, so core stays C++14.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "aepmapper.h"

#include "aep_parser.hpp"
#include "aep_riff.hpp"

#include "core/Private/document.h"
#include "core/canvas.h"
#include "core/Boxes/smartvectorpath.h"
#include "core/Boxes/rectangle.h"
#include "core/Boxes/circle.h"
#include "core/Boxes/polygonbox.h"
#include "core/Boxes/imagebox.h"
#include "core/Boxes/videobox.h"
#include "core/filesourcescache.h"
#include "core/Boxes/containerbox.h"
#include "core/Boxes/internallinkcanvas.h"
#include "core/Boxes/boundingbox.h"
#include "core/Animators/qrealanimator.h"
#include "core/Animators/qpointfanimator.h"
#include "core/Animators/transformanimator.h"
#include "core/appsupport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QDebug>
#include <QSet>
#include <QImage>

namespace {

using namespace glaxnimate::io::aep;

// ---------------------------------------------------------------------------
// Property helpers
// ---------------------------------------------------------------------------

using AepProperty = glaxnimate::io::aep::Property;
using AepPropertyValue = glaxnimate::io::aep::PropertyValue;

const AepPropertyValue* propertyValue(const PropertyBase* base)
{
    if (!base || base->class_type() != PropertyBase::Property) { return nullptr; }
    return &static_cast<const AepProperty*>(base)->value;
}

bool propertyBool(const PropertyBase* base, bool fallback = false)
{
    Q_UNUSED(base)
    return fallback;
}

qreal propertyReal(const PropertyBase* base, qreal fallback = 0)
{
    if (const auto* v = propertyValue(base)) {
        if (const auto* n = std::get_if<qreal>(&v->value)) { return *n; }
    }
    return fallback;
}

QPointF propertyPoint(const PropertyBase* base, const QPointF& fallback = {})
{
    if (const auto* v = propertyValue(base)) {
        if (const auto* p = std::get_if<QPointF>(&v->value)) { return *p; }
        if (const auto* v3 = std::get_if<QVector3D>(&v->value)) {
            return QPointF(v3->x(), v3->y());
        }
    }
    return fallback;
}

QColor propertyColor(const PropertyBase* base, const QColor& fallback = {})
{
    if (const auto* v = propertyValue(base)) {
        if (const auto* c = std::get_if<QColor>(&v->value)) { return *c; }
    }
    return fallback;
}

QString propertyString(const PropertyBase* base, const QString& fallback = {})
{
    Q_UNUSED(base)
    return fallback;
}

// Looks up a child property by match name, safe against non-group nodes.
const PropertyBase* groupProperty(const PropertyBase* base, const QString& key)
{
    if (!base || base->class_type() != PropertyBase::PropertyGroup) { return nullptr; }
    return static_cast<const PropertyGroup*>(base)->property(key);
}

int aepTimeToFrame(double time, double frameTime)
{
    if (frameTime <= 0) { return 0; }
    return qRound(time / frameTime);
}

// ---------------------------------------------------------------------------
// Keyframe support
// ---------------------------------------------------------------------------

// Writes a scalar AEP property's keyframes onto a QrealAnimator.
// Falls back to the static value when there are no keyframes.
void applyScalarProperty(QrealAnimator* anim,
                         const PropertyBase* prop,
                         double frameTime)
{
    if (!anim || !prop) { return; }
    if (prop->class_type() != PropertyBase::Property) { return; }
    const auto* p = static_cast<const AepProperty*>(prop);

    if (p->keyframes.empty()) {
        anim->setCurrentBaseValue(propertyReal(prop));
        return;
    }
    for (const auto& kf : p->keyframes) {
        const int frame = aepTimeToFrame(kf.time, frameTime);
        const qreal v = std::get_if<qreal>(&kf.value.value) ?
                    *std::get_if<qreal>(&kf.value.value) : 0.0;
        anim->saveValueToKey(frame, v);
    }
}

// Writes a 2D AEP property (position/scale/anchor) keyframes onto X/Y animators.
void applyPointProperty(QPointFAnimator* anim,
                        const PropertyBase* prop,
                        double frameTime)
{
    if (!anim || !prop) { return; }
    if (prop->class_type() != PropertyBase::Property) { return; }
    const auto* p = static_cast<const AepProperty*>(prop);

    if (p->keyframes.empty()) {
        const QPointF v = propertyPoint(prop);
        anim->getXAnimator()->setCurrentBaseValue(v.x());
        anim->getYAnimator()->setCurrentBaseValue(v.y());
        return;
    }
    for (const auto& kf : p->keyframes) {
        const int frame = aepTimeToFrame(kf.time, frameTime);
        QPointF v;
        if (const auto* p2 = std::get_if<QPointF>(&kf.value.value)) { v = *p2; }
        else if (const auto* v3 = std::get_if<QVector3D>(&kf.value.value)) {
            v = QPointF(v3->x(), v3->y());
        }
        anim->getXAnimator()->saveValueToKey(frame, v.x());
        anim->getYAnimator()->saveValueToKey(frame, v.y());
    }
}

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------

// AEP shape path string → SkPath. AEP paths use a dialect close to SVG;
// SkParsePath handles most of it. On failure we fall back to a rectangle.
SkPath aepPathToSkPath(const QString& pathStr)
{
    SkPath path;
    if (pathStr.isEmpty()) { return path; }
    // AEP path strings are typically "M ... C ... Z" with spaces; strip
    // the "{"..."}"/unicode markers some encodings add.
    QString cleaned = pathStr;
    cleaned.remove(QChar('{'));
    cleaned.remove(QChar('}'));
    cleaned = cleaned.trimmed();
    if (cleaned.isEmpty()) { return path; }
    SkParsePath::FromSVGString(cleaned.toStdString().data(), &path);
    return path;
}

// Builds an SkPath from an AEP BezierData property.
// AEP stores bezier points normalized to a bounding box, three points per
// vertex: anchor, in-tangent, out-tangent (in that order).
SkPath aepBezierToSkPath(const BezierData& bezier)
{
    SkPath path;
    if (bezier.points.empty()) { return path; }

    const auto convert = [&bezier](const QPointF& p) {
        return bezier.convert_point(p);
    };

    // First point group: anchor at index 0 (per glaxnimate's loader the
    // first point is the anchor of the first vertex).
    const int n = int(bezier.points.size());
    const auto& p0 = convert(bezier.points[0]);
    path.moveTo(SkFloatToScalar(p0.x()), SkFloatToScalar(p0.y()));

    int i = 1;
    while (i + 2 < n) {
        // AEP order per glaxnimate: points[i-1]=in tangent, points[i]=anchor,
        // points[i+1]=out tangent. glaxnimate uses the anchor as the cubic
        // start and the out tangent of the previous vertex + anchor of next.
        const auto& inTan = convert(bezier.points[i - 1]);
        const auto& anchor = convert(bezier.points[i]);
        const auto& outTan = convert(bezier.points[i + 1]);
        Q_UNUSED(inTan)
        path.cubicTo(SkFloatToScalar(outTan.x()), SkFloatToScalar(outTan.y()),
                     SkFloatToScalar(anchor.x()), SkFloatToScalar(anchor.y()),
                     SkFloatToScalar(anchor.x()), SkFloatToScalar(anchor.y()));
        i += 3;
    }

    if (bezier.closed) { path.close(); }
    return path;
}

void importPathShape(const PropertyBase* pathProp,
                     ContainerBox* parent,
                     const QString& name)
{
    if (!pathProp || !parent) { return; }
    if (pathProp->class_type() != PropertyBase::Property) { return; }
    const auto* prop = static_cast<const AepProperty*>(pathProp);
    const auto* bezier = std::get_if<BezierData>(&prop->value.value);
    if (!bezier) { return; }

    const SkPath skPath = aepBezierToSkPath(*bezier);
    if (skPath.isEmpty()) { return; }

    auto box = enve::make_shared<SmartVectorPath>();
    box->prp_setName(name.isEmpty() ? QStringLiteral("Shape") : name);
    box->getPathAnimator()->loadSkPath(skPath);
    // anchor set from AE transform
    parent->addContained(box);
}

void importRectShape(const PropertyBase* sizeProp,
                     const PropertyBase* posProp,
                     const PropertyBase* roundProp,
                     ContainerBox* parent,
                     const QString& name)
{
    if (!parent) { return; }
    const QPointF size = propertyPoint(sizeProp, {100, 100});
    const QPointF pos = propertyPoint(posProp, {0, 0});
    const QPointF round = propertyPoint(roundProp, {0, 0});

    auto rect = enve::make_shared<RectangleBox>();
    rect->prp_setName(name.isEmpty() ? QStringLiteral("Rectangle") : name);
    rect->setTopLeftPos(pos);
    rect->setBottomRightPos(pos + size);
    rect->setXRadius(round.x());
    rect->setYRadius(round.y());
    // anchor set from AE transform
    parent->addContained(rect);
}

void importEllipseShape(const PropertyBase* sizeProp,
                        const PropertyBase* posProp,
                        ContainerBox* parent,
                        const QString& name)
{
    if (!parent) { return; }
    const QPointF size = propertyPoint(sizeProp, {100, 100});
    const QPointF pos = propertyPoint(posProp, {0, 0});

    auto circle = enve::make_shared<Circle>();
    circle->prp_setName(name.isEmpty() ? QStringLiteral("Ellipse") : name);
    circle->setHorizontalRadius(size.x() / 2.0);
    circle->setVerticalRadius(size.y() / 2.0);
    circle->setCenter(pos);
    // anchor set from AE transform
    parent->addContained(circle);
}

void importPolystarShape(const PropertyBase* pointsProp,
                         const PropertyBase* posProp,
                         const PropertyBase* outerRadiusProp,
                         const PropertyBase* innerRadiusProp,
                         ContainerBox* parent,
                         const QString& name)
{
    if (!parent) { return; }
    const int points = qMax(3, qRound(propertyReal(pointsProp, 5)));
    const QPointF pos = propertyPoint(posProp, {0, 0});
    const qreal outerR = propertyReal(outerRadiusProp, 50);
    const qreal innerR = propertyReal(innerRadiusProp, outerR * 0.4);

    auto star = enve::make_shared<PolygonBox>();
    star->prp_setName(name.isEmpty() ? QStringLiteral("Polystar") : name);
    star->setSideCount(points);
    // PolygonBox uses a horizontal radius for outer vertices; approximate
    // with the outer radius for a regular polygon.
    star->setHorizontalRadius(outerR);
    star->setVerticalRadius(outerR);
    star->setCenter(pos);
    // anchor set from AE transform
    parent->addContained(star);
    Q_UNUSED(innerR)
}

// ---------------------------------------------------------------------------
// Transform
// ---------------------------------------------------------------------------

void applyLayerTransform(BoundingBox* box,
                         const Layer& layer,
                         double frameTime)
{
    auto* ta = box->getBoxTransformAnimator();
    if (!ta) { return; }

    // AEP stores layer transforms in the "ADBE Transform Group" property
    // group. Match child properties by name suffix, like glaxnimate does.
    const auto* transformGroup =
            groupProperty(&layer.properties, QStringLiteral("ADBE Transform Group"));
    if (!transformGroup ||
        transformGroup->class_type() != PropertyBase::PropertyGroup) {
        // Some layers (e.g. solids) put transforms directly on the layer.
        transformGroup = &layer.properties;
    }
    const auto* g = static_cast<const PropertyGroup*>(transformGroup);

    for (const auto& p : g->properties) {
        if (!p.value) { continue; }
        const QString& mn = p.match_name;
        if (mn.endsWith(QStringLiteral("Position")) && !mn.endsWith(QStringLiteral("Position_0")) &&
            !mn.endsWith(QStringLiteral("Position_1")) && !mn.endsWith(QStringLiteral("Position_2"))) {
            // AE position is the anchor point's composition coordinate.
            // friction's transform origin (pos) is the pivot/anchor, so
            // mapping is direct. Handle static case via setAbsolutePos
            // (parent transform aware); keyframed case writes animators.
            if (p.value->class_type() == PropertyBase::Property) {
                const auto* prop = static_cast<const AepProperty*>(p.value.get());
                if (prop->keyframes.empty()) {
                    const QPointF pos = propertyPoint(p.value.get(), {0, 0});
                    box->setAbsolutePos(pos);
                } else {
                    applyPointProperty(ta->getPosAnimator(), p.value.get(), frameTime);
                }
            }
        } else if (mn.endsWith(QStringLiteral("Anchor Point")) ||
                   mn.endsWith(QStringLiteral("Anchor"))) {
            // AE anchor is relative to the layer's top-left; friction's
            // pivot is also relative to the box top-left → direct map.
            if (p.value->class_type() == PropertyBase::Property) {
                const auto* prop = static_cast<const AepProperty*>(p.value.get());
                if (prop->keyframes.empty()) {
                    const QPointF anchor = propertyPoint(p.value.get(), {0, 0});
                    box->setPivotRelPos(anchor);
                } else {
                    applyPointProperty(ta->getPivotAnimator(), p.value.get(), frameTime);
                }
            }
        } else if (mn.endsWith(QStringLiteral("Scale")) &&
                   !mn.endsWith(QStringLiteral("Scale_0")) &&
                   !mn.endsWith(QStringLiteral("Scale_1"))) {
            // AEP layer scale is stored as a multiplier (1.0 = 100%),
            // matching friction's transform animator, so no conversion.
            if (p.value->class_type() == PropertyBase::Property) {
                const auto* prop = static_cast<const AepProperty*>(p.value.get());
                if (prop->keyframes.empty()) {
                    const QPointF s = propertyPoint(p.value.get(), {1, 1});
                    ta->setScale(s.x(), s.y());
                }
            }
        } else if (mn.endsWith(QStringLiteral("Rotation")) ||
                   mn.endsWith(QStringLiteral("Rotate Z"))) {
            applyScalarProperty(ta->getRotAnimator(), p.value.get(), frameTime);
        } else if (mn.endsWith(QStringLiteral("Opacity")) &&
                   !mn.endsWith(QStringLiteral("Opacity 1")) &&
                   !mn.endsWith(QStringLiteral("Opacity 2"))) {
            auto* opAnim = ta->getOpacityAnimator();
            if (opAnim && p.value->class_type() == PropertyBase::Property) {
                const auto* prop = static_cast<const AepProperty*>(p.value.get());
                if (prop->keyframes.empty()) {
                    opAnim->setCurrentBaseValue(
                                qBound<qreal>(0.0, propertyReal(p.value.get(), 100.0), 100.0));
                }
            }
        }
    }
}

SkBlendMode aepBlendMode(int mode)
{
    // AE blend mode ids (from glaxnimate's AEP loader table).
    switch (mode) {
    case 2: return SkBlendMode::kSrcOver;      // Normal
    case 5: return SkBlendMode::kMultiply;
    case 6: return SkBlendMode::kScreen;
    case 7: return SkBlendMode::kOverlay;
    case 8: return SkBlendMode::kSoftLight;
    case 9: return SkBlendMode::kHardLight;
    case 10: return SkBlendMode::kDarken;
    case 11: return SkBlendMode::kLighten;
    case 26: return SkBlendMode::kDifference;
    case 25: return SkBlendMode::kExclusion;
    case 27: return SkBlendMode::kColorDodge;
    case 28: return SkBlendMode::kColorBurn;
    case 13: return SkBlendMode::kHue;
    case 14: return SkBlendMode::kSaturation;
    case 15: return SkBlendMode::kColor;
    case 16: return SkBlendMode::kLuminosity;
    case 4: return SkBlendMode::kPlus;         // Add
    case 12: return SkBlendMode::kDifference;  // Classic Difference
    case 30: return SkBlendMode::kColorBurn;   // Linear Burn
    case 29: return SkBlendMode::kColorDodge;  // Linear Dodge
    default: return SkBlendMode::kSrcOver;     // Normal / unknown
    }
}

// ---------------------------------------------------------------------------
// Import context
// ---------------------------------------------------------------------------

// Footage extraction: AEP embeds assets; for friction we need footage on
// disk (ImageBox takes a file path). We keep a per-import temp dir and dump
// any embedded asset bytes we can find.
class FootageCache
{
public:
    QString dir()
    {
        if (!mDir) {
            mDir = std::make_unique<QTemporaryDir>(
                        QDir::tempPath() + "/friction_aep_XXXXXX");
        }
        return mDir->path();
    }

    // Resolves a footage asset path. AEP stores Windows drive paths
    // (e.g. "D:/project/file.psd"); on this machine the D: drive maps to
    // /media/kinect/Project. We try, in order:
    //   1) the stored path as-is
    //   2) Windows drive letter rewritten to the mount point
    //   3) "collected asset" next to the AEP file
    //   4) same file name under the AEP file's parent PSD/ subdirectory
    QString saveAsset(const FolderItem* item, const QFileInfo& aepFile)
    {
        const auto* fileAsset = dynamic_cast<const FileAsset*>(item);
        if (!fileAsset) { return QString(); }

        const QString stored = fileAsset->path.absoluteFilePath();
        const QString name = fileAsset->path.fileName();

        if (QFile::exists(stored)) {
            return stored;
        }

        // Rewrite Windows drive paths: "D:/x" or "/D:/x" → "/media/kinect/Project/x".
        QString winPath = fileAsset->path.filePath();
        if (winPath.startsWith('/')) { winPath = winPath.mid(1); } // "/D:/x"
        if (winPath.size() >= 3 && winPath.at(1) == ':') {
            const QString rel = winPath.mid(3); // after "D:"
            const QStringList mounts = {
                QStringLiteral("/media/kinect/Project"),
                QStringLiteral("/mnt"),
            };
            for (const auto& m : mounts) {
                const QString mapped = QDir(m).filePath(rel);
                if (QFile::exists(mapped)) {
                    return mapped;
                }
            }
        }

        // Collected asset next to the AEP file.
        const QString collected = QDir(aepFile.absolutePath()).filePath(name);
        if (!name.isEmpty() && QFile::exists(collected)) {
            return collected;
        }

        // Look in a "PSD" subdirectory of the AEP's parent directory
        // (common layout: project/ + project/PSD/*.psd).
        const QString parentDir = QFileInfo(aepFile.absolutePath()).absolutePath();
        const QString psdDir = QDir(parentDir).filePath("PSD");
        const QString inPsd = QDir(psdDir).filePath(name);
        if (!name.isEmpty() && QFile::exists(inPsd)) {
            return inPsd;
        }

        qWarning() << "[aep-import] footage not found:" << stored
                   << "collected:" << collected;
        return QString();
    }

private:
    std::unique_ptr<QTemporaryDir> mDir;
};

struct ImportContext
{
    double frameTime = 0.0;
    QFileInfo aepFile;
    FootageCache* footage = nullptr;
    std::unordered_map<Id, Canvas*> compCanvasMap;
    std::unordered_map<Id, FolderItem*> assetMap;
};

// Import a single layer into a target container. precompLink targets are
// resolved after all comps exist.
BoundingBox* importLayer(ImportContext& ctx,
                         ContainerBox* parent,
                         Canvas* parentScene,
                         const Layer& layer)
{
    if (!layer.visible || layer.is_guide) { return nullptr; }
    if (layer.type == LayerType::LightLayer ||
        layer.type == LayerType::CameraLayer) {
        // Not supported in friction (no light/camera boxes).
        return nullptr;
    }

    BoundingBox* box = nullptr;

    switch (layer.type) {
    case LayerType::ShapeLayer: {
        const auto* contents = layer.properties.property("Contents");
        if (contents) {
            // Shape layers: iterate groups; each group has "Shapes".
            for (const auto& pair : *contents) {
                if (!pair.value) { continue; }
                const auto* group = pair.value.get();
                const auto* shapes = group && group->class_type() == PropertyBase::PropertyGroup
                        ? static_cast<const PropertyGroup*>(group)->property("Shapes")
                        : nullptr;
                if (!shapes) { continue; }
                for (const auto& sp : *shapes) {
                    if (!sp.value) { continue; }
                    const auto* shapeProp = sp.value.get();
                    const QString shapeName = sp.match_name;
                    if (const auto* path = groupProperty(shapeProp, "Path")) {
                        importPathShape(path, parent, shapeName);
                    } else if (groupProperty(shapeProp, "Size") &&
                               groupProperty(shapeProp, "Position")) {
                        const auto* r = groupProperty(shapeProp, "Roundness");
                        importRectShape(groupProperty(shapeProp, "Size"),
                                        groupProperty(shapeProp, "Position"),
                                        r, parent, shapeName);
                    } else if (groupProperty(shapeProp, "Size") &&
                               groupProperty(shapeProp, "Position") &&
                               groupProperty(shapeProp, "Inner Radius")) {
                        importPolystarShape(groupProperty(shapeProp, "Points"),
                                            groupProperty(shapeProp, "Position"),
                                            groupProperty(shapeProp, "Outer Radius"),
                                            groupProperty(shapeProp, "Inner Radius"),
                                            parent, shapeName);
                    } else if (groupProperty(shapeProp, "Size") &&
                               groupProperty(shapeProp, "Position")) {
                        importEllipseShape(groupProperty(shapeProp, "Size"),
                                           groupProperty(shapeProp, "Position"),
                                           parent, shapeName);
                    }
                }
            }
        }
        break;
    }
    case LayerType::AssetLayer: {
        if (layer.asset_id == 0) { break; }
        auto it = ctx.compCanvasMap.find(layer.asset_id);
        if (it != ctx.compCanvasMap.end()) {
            // Precomp reference: create an internal link box.
            auto* target = it->second;
            if (target) {
                auto link = target->createLink(false);
                // Collapse ON (Group mode): show precomp content without
                // clipping. The default (clipToCanvas=true) hides content.
                if (auto* ilc = enve_cast<InternalLinkCanvas*>(link.get())) {
                    ilc->clipToCanvasProperty()->setValue(false);
                }
                link->setPivotRelPos(QPointF(target->getCanvasWidth() / 2.0,
                                             target->getCanvasHeight() / 2.0));
                link->prp_setName(layer.name.isEmpty() ?
                                      target->prp_getName() : layer.name);
                box = link.get();
                parent->addContained(link);
            }
        } else {
            // Footage asset → ImageBox.
            auto ait = ctx.assetMap.find(layer.asset_id);
            if (ait != ctx.assetMap.end() && ait->second && ctx.footage) {
                const QString path = ctx.footage->saveAsset(ait->second,
                                                            ctx.aepFile);
                if (!path.isEmpty()) {
                    const QString ext = QFileInfo(path).suffix().toLower();
                    if (isVideoExt(ext)) {
                        // Video footage → VideoBox (playable).
                        auto vid = enve::make_shared<VideoBox>();
                        vid->setFilePath(path);
                        vid->prp_setName(layer.name.isEmpty() ?
                                             QStringLiteral("Footage") : layer.name);
                        box = vid.get();
                        parent->addContained(vid);
                    } else {
                        auto img = enve::make_shared<ImageBox>(path);
                        img->prp_setName(layer.name.isEmpty() ?
                                             QStringLiteral("Footage") : layer.name);
                        // Anchor is set from AE's Anchor Point in
                        // applyLayerTransform; do not override it here.
                        box = img.get();
                        parent->addContained(img);
                    }
                }
            }
        }
        break;
    }
    case LayerType::TextLayer:
    default:
        break;
    }

    if (box) {
        applyLayerTransform(box, layer, ctx.frameTime);
        if (layer.blend_mode > 0) {
            box->setBlendModeSk(aepBlendMode(layer.blend_mode));
        }
    }
    return box;
}

} // namespace

// ---------------------------------------------------------------------------
// AepMapper public API
// ---------------------------------------------------------------------------

namespace AepModule {

qsptr<BoundingBox> importAepFile(const QFileInfo& fileInfo,
                                 Canvas* const scene)
{
    if (!scene || !Document::sInstance) { return nullptr; }

    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "AEP import: cannot open" << fileInfo.absoluteFilePath();
        return nullptr;
    }

    try {
        FootageCache footageCache;
        glaxnimate::io::ImportExport io;
        glaxnimate::io::aep::AepRiff riff;
        RiffChunk chunk = riff.parse(&file);
        // NOTE: keep the file open! RiffChunk's BinaryReader reads lazily
        // from the device; closing it here breaks parser.parse below.
        // The QFile closes automatically at the end of the scope.

        glaxnimate::io::aep::AepParser parser(&io);
        Project project = parser.parse(chunk);

        if (project.compositions.empty()) {
            qWarning() << "AEP import: no compositions found";
            return nullptr;
        }

        ImportContext ctx;
        ctx.frameTime = project.compositions.front()->frame_time;
        ctx.aepFile = fileInfo;
        ctx.footage = &footageCache;

        // Register all assets (footage/solids) for layer lookup.
        for (const auto& pair : project.assets) {
            ctx.assetMap[pair.first] = pair.second;
        }

        // Stage 1: create a friction Canvas for every AEP composition.
        int createdCount = 0;
        for (auto* comp : project.compositions) {
            if (!comp) { continue; }
            auto* newScene = Document::sInstance->createNewScene(false);
            if (!newScene) { continue; }
            newScene->prp_setName(comp->name.isEmpty() ?
                                      QStringLiteral("Comp") : comp->name);
            // AEP stores the composition size in width/height (the Lottie
            // block); resolution_x/y are not reliably populated by the parser.
            int w = comp->width > 0 ? qRound(comp->width) : scene->getCanvasWidth();
            int h = comp->height > 0 ? qRound(comp->height) : scene->getCanvasHeight();
            newScene->setCanvasSize(w, h);
            if (comp->framerate > 0) { newScene->setFps(comp->framerate); }
            else { newScene->setFps(scene->getFps()); }
            if (comp->frame_time > 0) { ctx.frameTime = comp->frame_time; }
            newScene->planCenterPivotPosition();
            ctx.compCanvasMap[comp->id] = newScene;
            ++createdCount;
        }
        qWarning() << "[aep-import] stage1 created scenes:" << createdCount
                   << "of" << project.compositions.size();

        // Stage 2: fill every composition canvas with its layers.
        int filledLayers = 0;
        for (auto* comp : project.compositions) {
            if (!comp) { continue; }
            auto cit = ctx.compCanvasMap.find(comp->id);
            if (cit == ctx.compCanvasMap.end() || !cit->second) { continue; }
            Canvas* canvas = cit->second;
            ctx.frameTime = comp->frame_time > 0 ? comp->frame_time : ctx.frameTime;

            // Layers in AEP are top-to-bottom; friction addContained appends
            // on top, so iterate in reverse to preserve stacking order.
            for (auto it = comp->layers.rbegin(); it != comp->layers.rend(); ++it) {
                if (importLayer(ctx, canvas, canvas, **it)) { ++filledLayers; }
            }
        }
        qWarning() << "[aep-import] stage2 filled layers:" << filledLayers;

        // Stage 3: link only the top-level compositions (those not used as
        // a nested precomp layer anywhere). Nested precomps are already
        // referenced through InternalLinkCanvas boxes inside their parents,
        // so linking every comp flat here would duplicate them.
        QSet<Id> referencedComps;
        for (auto* comp : project.compositions) {
            if (!comp) { continue; }
            for (const auto& layer : comp->layers) {
                if (!layer) { continue; }
                if (layer->type == LayerType::AssetLayer &&
                    ctx.compCanvasMap.count(layer->asset_id)) {
                    referencedComps.insert(layer->asset_id);
                }
            }
        }

        int linkedCount = 0;
        for (auto* comp : project.compositions) {
            if (!comp) { continue; }
            if (referencedComps.contains(comp->id)) { continue; } // nested
            auto cit = ctx.compCanvasMap.find(comp->id);
            if (cit == ctx.compCanvasMap.end() || !cit->second) { continue; }
            Canvas* compScene = cit->second;
            auto link = compScene->createLink(false);
            // Collapse ON (Group mode): show precomp content.
            if (auto* ilc = enve_cast<InternalLinkCanvas*>(link.get())) {
                ilc->clipToCanvasProperty()->setValue(false);
            }
            link->setPivotRelPos(QPointF(compScene->getCanvasWidth() / 2.0,
                                         compScene->getCanvasHeight() / 2.0));
            link->prp_setName(compScene->prp_getName());
            // Add into the host scene so it shows up in the project.
            scene->addContained(link);
            ++linkedCount;
        }
        qWarning() << "[aep-import] stage3 top-level comps:" << linkedCount;
        // All links are already in the host scene; return null so the
        // caller does not insert an extra copy.
        return nullptr;

    } catch (const std::exception& e) {
        qWarning() << "AEP import failed:" << e.what();
        return nullptr;
    }
}

} // namespace AepModule
