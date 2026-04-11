#include "aeshortcutdefaults.h"

#include "appsupport.h"

QList<AeShortcutDefinition> aeShortcutDefinitions()
{
    return {
        {QStringLiteral("boxTransform"), QStringLiteral("Tools"), QObject::tr("Selection Tool"), QObject::tr("Primary layer selection / transform tool."), QStringLiteral("V")},
        {QStringLiteral("pointTransform"), QStringLiteral("Tools"), QObject::tr("Anchor / Point Tool"), QObject::tr("Anchor point editing / point transform tool."), QStringLiteral("Y")},
        {QStringLiteral("pathCreate"), QStringLiteral("Tools"), QObject::tr("Pen Tool"), QObject::tr("AE-style pen tool. Click to create/extend a path, click a segment to add a vertex, Alt-click a point to delete a vertex."), QStringLiteral("G")},
        {QStringLiteral("drawPath"), QStringLiteral("Tools"), QObject::tr("Freehand Path Tool"), QObject::tr("Freehand drawing / fitting tool. Kept as a secondary path creation mode."), QStringLiteral("Shift+G")},
        {QStringLiteral("rectMode"), QStringLiteral("Tools"), QObject::tr("Shape Tool Toggle"), QObject::tr("AE-style Q toggle for rectangle / ellipse."), QStringLiteral("Q")},
        {QStringLiteral("textMode"), QStringLiteral("Tools"), QObject::tr("Text Tool"), QObject::tr("Text creation tool."), QStringLiteral("Ctrl+T")},
        {QStringLiteral("aeToolHand"), QStringLiteral("Tools"), QObject::tr("Hand Tool"), QObject::tr("Viewer pan shortcut hint."), QStringLiteral("H")},
        {QStringLiteral("aeToolZoom"), QStringLiteral("Tools"), QObject::tr("Zoom Tool"), QObject::tr("Viewer zoom shortcut hint."), QStringLiteral("Z")},
        {QStringLiteral("aeToolRotate"), QStringLiteral("Tools"), QObject::tr("Rotate Tool"), QObject::tr("Rotate tool shortcut routed to selection gizmo."), QStringLiteral("W")},
        {QStringLiteral("aeRevealAnchor"), QStringLiteral("Reveal"), QObject::tr("Reveal Anchor Point"), QObject::tr("Expand anchor / pivot property."), QStringLiteral("A")},
        {QStringLiteral("aeRevealPosition"), QStringLiteral("Reveal"), QObject::tr("Reveal Position"), QObject::tr("Expand position / translation property."), QStringLiteral("P")},
        {QStringLiteral("aeRevealScale"), QStringLiteral("Reveal"), QObject::tr("Reveal Scale"), QObject::tr("Expand scale property."), QStringLiteral("S")},
        {QStringLiteral("aeRevealRotation"), QStringLiteral("Reveal"), QObject::tr("Reveal Rotation"), QObject::tr("Expand rotation property."), QStringLiteral("R")},
        {QStringLiteral("aeRevealOpacity"), QStringLiteral("Reveal"), QObject::tr("Reveal Opacity"), QObject::tr("Expand opacity property."), QStringLiteral("T")},
        {QStringLiteral("aeRevealMasks"), QStringLiteral("Reveal"), QObject::tr("Reveal Masks"), QObject::tr("Expand mask properties on selected layer(s)."), QStringLiteral("M")},
        {QStringLiteral("aeRevealAnimated"), QStringLiteral("Reveal"), QObject::tr("Reveal Animated / Modified"), QObject::tr("Single press shows keyed properties; double press shows modified properties."), QStringLiteral("U")},
        {QStringLiteral("aeCenterPivot"), QStringLiteral("Transform"), QObject::tr("Center Pivot"), QObject::tr("Move the anchor / pivot of the selected layer(s) to the visual center."), QStringLiteral("Ctrl+Home")},
        {QStringLiteral("aeAlignCenter"), QStringLiteral("Transform"), QObject::tr("Align to Composition Center"), QObject::tr("Align selected layer(s) horizontally and vertically to the composition center."), QStringLiteral("Ctrl+Alt+Home")},
        {QStringLiteral("aeCompSettings"), QStringLiteral("Composition"), QObject::tr("Composition Settings"), QObject::tr("Open composition settings."), QStringLiteral("Ctrl+K")},
        {QStringLiteral("aeAddRenderQueue"), QStringLiteral("Composition"), QObject::tr("Add to Render Queue"), QObject::tr("Send active composition to render queue."), QStringLiteral("Ctrl+M")},
        {QStringLiteral("aeTimeRemap"), QStringLiteral("Timeline"), QObject::tr("Toggle Time Remapping"), QObject::tr("Enable or disable time remapping on selected footage layers."), QStringLiteral("Ctrl+Alt+T")},
        {QStringLiteral("aeFreezeFrame"), QStringLiteral("Timeline"), QObject::tr("Freeze Frame"), QObject::tr("Freeze selected footage layers from the current time to the layer out point."), QStringLiteral("Ctrl+Alt+Shift+F")},
        {QStringLiteral("aeFitComp"), QStringLiteral("Composition"), QObject::tr("Fit Composition"), QObject::tr("Fit viewer to available area."), QStringLiteral("Shift+/")},
        {QStringLiteral("rewind"), QStringLiteral("Timeline"), QObject::tr("Go to First Frame"), QObject::tr("Jump to composition start."), QStringLiteral("Shift+Left")},
        {QStringLiteral("fastForward"), QStringLiteral("Timeline"), QObject::tr("Go to Last Frame"), QObject::tr("Jump to composition end."), QStringLiteral("Shift+Right")},
        {QStringLiteral("previewSVG"), QStringLiteral("Legacy"), QObject::tr("Preview SVG"), QObject::tr("Preview SVG export in browser."), QStringLiteral("Ctrl+F12")},
        {QStringLiteral("exportSVG"), QStringLiteral("Legacy"), QObject::tr("Export SVG"), QObject::tr("Export SVG animation."), QStringLiteral("Shift+F12")}
    };
}

QString aeShortcutDefault(const QString &id,
                         const QString &fallback)
{
    const auto defs = aeShortcutDefinitions();
    for (const auto &def : defs) {
        if (def.id == id) {
            return def.defaultSequence;
        }
    }
    return fallback;
}

QKeySequence aeShortcutSequence(const QString &id,
                                const QString &fallback)
{
    return QKeySequence(AppSupport::getSettings(QStringLiteral("shortcuts"),
                                                id,
                                                aeShortcutDefault(id, fallback)).toString());
}
