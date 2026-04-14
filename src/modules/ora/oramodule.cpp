#include "oramodule.h"

#include "../../core/Boxes/containerbox.h"
#include "../../core/Boxes/imagebox.h"
#include "../../core/FileCacheHandlers/filecachehandler.h"
#include "../../core/appsupport.h"
#include "../../core/canvas.h"
#include "../../core/exceptions.h"
#include "../../core/Private/document.h"
#include "../../core/fileshandler.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSet>
#include <QTimer>

namespace {

QHash<int, int> gOraSceneParentIds;
bool gOraSceneParentCacheLoaded = false;

QString oraCacheDirForFile(const QFileInfo &fileInfo)
{
    const QByteArray key = QFileInfo(fileInfo.absoluteFilePath()).canonicalFilePath().isEmpty() ?
                fileInfo.absoluteFilePath().toUtf8() :
                QFileInfo(fileInfo.absoluteFilePath()).canonicalFilePath().toUtf8();
    const QString hash = QString::fromUtf8(
                QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());
    const QString root = QDir(AppSupport::getAppConfigPath()).filePath("OraImports");
    QDir().mkpath(root);
    return QDir(root).filePath(hash);
}

void writeOraImportMetadata(const QFileInfo &fileInfo,
                            const QString &extractDir)
{
    QSettings meta(QDir(extractDir).filePath(QStringLiteral(".friction_ora_import.ini")),
                   QSettings::IniFormat);
    meta.setValue(QStringLiteral("ora/displayName"), fileInfo.fileName());
    meta.setValue(QStringLiteral("ora/baseName"), fileInfo.completeBaseName());
    meta.setValue(QStringLiteral("ora/sourcePath"), fileInfo.absoluteFilePath());
    meta.remove(QStringLiteral("ora/sceneIds"));
    meta.remove(QStringLiteral("ora/rootSceneId"));
    meta.remove(QStringLiteral("ora/sceneParents"));
    meta.sync();
}

void registerOraSceneMetadata(const QString &extractDir,
                              Canvas * const scene,
                              const bool rootScene,
                              Canvas * const parentScene = nullptr)
{
    if (!scene) { return; }
    QSettings meta(QDir(extractDir).filePath(QStringLiteral(".friction_ora_import.ini")),
                   QSettings::IniFormat);
    QVariantList sceneIds = meta.value(QStringLiteral("ora/sceneIds")).toList();
    const int sceneId = scene->getDocumentId();
    bool found = false;
    for (const auto &value : sceneIds) {
        if (value.toInt() == sceneId) {
            found = true;
            break;
        }
    }
    if (!found) {
        sceneIds.append(sceneId);
        meta.setValue(QStringLiteral("ora/sceneIds"), sceneIds);
    }
    const int parentSceneId = parentScene ? parentScene->getDocumentId() : -1;
    meta.setValue(QStringLiteral("ora/sceneParents/%1").arg(sceneId), parentSceneId);
    gOraSceneParentIds.insert(sceneId, parentSceneId);
    gOraSceneParentCacheLoaded = true;
    if (rootScene) {
        meta.setValue(QStringLiteral("ora/rootSceneId"), sceneId);
    }
    meta.sync();
}

void loadOraSceneParentCache()
{
    if (gOraSceneParentCacheLoaded) {
        return;
    }
    gOraSceneParentCacheLoaded = true;

    const QDir rootDir(QDir(AppSupport::getAppConfigPath()).filePath(QStringLiteral("OraImports")));
    if (!rootDir.exists()) {
        return;
    }

    const auto importDirs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &dirName : importDirs) {
        QSettings meta(rootDir.filePath(dirName + QDir::separator() +
                                        QStringLiteral(".friction_ora_import.ini")),
                       QSettings::IniFormat);
        const QStringList keys = meta.allKeys();
        for (const QString &key : keys) {
            if (!key.startsWith(QStringLiteral("ora/sceneParents/"))) {
                continue;
            }
            bool sceneOk = false;
            const int sceneId = key.mid(QStringLiteral("ora/sceneParents/").size()).toInt(&sceneOk);
            if (!sceneOk) {
                continue;
            }
            bool parentOk = false;
            const int parentSceneId = meta.value(key).toInt(&parentOk);
            gOraSceneParentIds.insert(sceneId, parentOk ? parentSceneId : -1);
        }
    }
}

QList<int> oraSceneNavigationChainIdsForSceneId(const int sceneId)
{
    loadOraSceneParentCache();
    if (!gOraSceneParentIds.contains(sceneId)) {
        return {};
    }

    QList<int> chain;
    QSet<int> visited;
    int cursor = sceneId;
    while (cursor >= 0 && !visited.contains(cursor)) {
        visited.insert(cursor);
        chain.prepend(cursor);
        if (!gOraSceneParentIds.contains(cursor)) {
            break;
        }
        cursor = gOraSceneParentIds.value(cursor, -1);
    }
    return chain;
}

QString oraElementName(const QDomElement &element)
{
    const QString directName = element.attribute(QStringLiteral("name")).trimmed();
    if (!directName.isEmpty()) {
        return directName;
    }

    const QString directLabel = element.attribute(QStringLiteral("label")).trimmed();
    if (!directLabel.isEmpty()) {
        return directLabel;
    }

    const QDomNamedNodeMap attrs = element.attributes();
    for (int i = 0; i < attrs.count(); ++i) {
        const QDomNode attr = attrs.item(i);
        const QString key = attr.nodeName().trimmed().toLower();
        if ((key.endsWith(QStringLiteral(":name")) ||
             key.endsWith(QStringLiteral(":label")) ||
             key == QStringLiteral("name") ||
             key == QStringLiteral("label")) &&
            !attr.nodeValue().trimmed().isEmpty()) {
            return attr.nodeValue().trimmed();
        }
    }

    return QString();
}

void extractOraArchive(const QFileInfo &fileInfo,
                       const QString &extractDir);

class OraHotReloadManager final : public QObject
{
public:
    static OraHotReloadManager &instance()
    {
        static OraHotReloadManager manager;
        return manager;
    }

    void watchImport(const QFileInfo &fileInfo,
                     const QString &extractDir)
    {
        const QString sourcePath = fileInfo.absoluteFilePath();
        mSourceToExtractDir[sourcePath] = extractDir;
        if (!mWatcher.files().contains(sourcePath)) {
            mWatcher.addPath(sourcePath);
        }
    }

private:
    OraHotReloadManager()
    {
        connect(&mWatcher, &QFileSystemWatcher::fileChanged,
                this, [this](const QString &path) {
            if (path.isEmpty()) { return; }
            QTimer::singleShot(200, this, [this, path]() {
                refreshImport(path);
            });
        });
    }

    void refreshImport(const QString &sourcePath)
    {
        const QString extractDir = mSourceToExtractDir.value(sourcePath);
        if (extractDir.isEmpty()) { return; }

        const QFileInfo info(sourcePath);
        if (!info.exists()) {
            if (!mWatcher.files().contains(sourcePath)) {
                mWatcher.addPath(sourcePath);
            }
            return;
        }

        try {
            extractOraArchive(info, extractDir);
            writeOraImportMetadata(info, extractDir);
        } catch (const std::exception &e) {
            gPrintExceptionCritical(e);
        }

        if (!mWatcher.files().contains(sourcePath)) {
            mWatcher.addPath(sourcePath);
        }

        if (auto *filesHandler = FilesHandler::sInstance) {
            const QString prefix = QDir(extractDir).absolutePath() + QDir::separator();
            const auto handlers = filesHandler->fileHandlers();
            for (auto *handler : handlers) {
                if (!handler) { continue; }
                const QString handlerPath = QFileInfo(handler->path()).absoluteFilePath();
                if (handlerPath.startsWith(prefix)) {
                    handler->reloadAction();
                }
            }
        }
    }

    QFileSystemWatcher mWatcher;
    QHash<QString, QString> mSourceToExtractDir;
};

bool runOraHelper(const QString &program,
                  const QStringList &arguments,
                  QString *stdOut = nullptr,
                  QString *stdErr = nullptr)
{
    if (program.isEmpty()) { return false; }
    QProcess proc;
    proc.setProgram(program);
    proc.setArguments(arguments);
    proc.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    proc.start();
    if (!proc.waitForStarted()) {
        return false;
    }
    proc.closeWriteChannel();
    proc.waitForFinished(-1);
    if (stdOut) { *stdOut = QString::fromUtf8(proc.readAllStandardOutput()); }
    if (stdErr) { *stdErr = QString::fromUtf8(proc.readAllStandardError()); }
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

void extractOraArchive(const QFileInfo &fileInfo,
                       const QString &extractDir)
{
    QDir dir(extractDir);
    if (dir.exists()) {
        dir.removeRecursively();
    }
    QDir().mkpath(extractDir);

    QString stdErr;
    const QString archivePath = fileInfo.absoluteFilePath();

    if (runOraHelper(QStringLiteral("unzip"),
                     {QStringLiteral("-qq"),
                      QStringLiteral("-o"),
                      archivePath,
                      QStringLiteral("-d"),
                      extractDir},
                     nullptr,
                     &stdErr)) {
        return;
    }

    if (runOraHelper(QStringLiteral("bsdtar"),
                     {QStringLiteral("-xf"),
                      archivePath,
                      QStringLiteral("-C"),
                      extractDir},
                     nullptr,
                     &stdErr)) {
        return;
    }

    const QString pyScript =
            QStringLiteral(
                "import sys, zipfile; "
                "zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])");
    if (runOraHelper(QStringLiteral("python3"),
                     {QStringLiteral("-c"), pyScript, archivePath, extractDir},
                     nullptr,
                     &stdErr)) {
        return;
    }

    RuntimeThrow(QObject::tr("Unable to extract ORA archive.\n"
                             "Tried unzip / bsdtar / python3.\n%1").arg(stdErr));
}

QDomDocument loadOraStackDocument(const QString &extractDir)
{
    QFile stackFile(QDir(extractDir).filePath(QStringLiteral("stack.xml")));
    if (!stackFile.open(QIODevice::ReadOnly)) {
        RuntimeThrow(QObject::tr("ORA import failed: stack.xml not found."));
    }

    QDomDocument doc;
    QString parseError;
    int errorLine = 0;
    int errorColumn = 0;
    if (!doc.setContent(&stackFile, &parseError, &errorLine, &errorColumn)) {
        RuntimeThrow(QObject::tr("ORA import failed: stack.xml parse error at %1:%2\n%3")
                     .arg(errorLine)
                     .arg(errorColumn)
                     .arg(parseError));
    }
    return doc;
}

void applyOraCommonAttributes(BoundingBox *box,
                              const QDomElement &element)
{
    if (!box) { return; }

    const QString name = oraElementName(element);
    if (!name.isEmpty()) {
        box->prp_setName(name);
    }

    const QString visibility = element.attribute(QStringLiteral("visibility"),
                                                 QStringLiteral("visible")).toLower();
    box->setVisible(visibility != QStringLiteral("hidden"));

    bool ok = false;
    const qreal opacity = element.attribute(QStringLiteral("opacity"),
                                            QStringLiteral("1.0")).toDouble(&ok);
    if (ok) {
        box->setOpacity(qBound<qreal>(0.0, opacity * 100.0, 100.0));
    }
}

Canvas *createOraSceneTemplate(Canvas * const sceneTemplate,
                               const QString &sceneName,
                               const int canvasWidth,
                               const int canvasHeight,
                               const QString &extractDir,
                               const bool rootScene = false,
                               Canvas * const parentScene = nullptr)
{
    if (!sceneTemplate || !Document::sInstance) {
        return nullptr;
    }

    auto *newScene = Document::sInstance->createNewScene(false);
    if (!newScene) {
        return nullptr;
    }

    newScene->prp_setName(sceneName);
    newScene->setCanvasSize(canvasWidth, canvasHeight);
    newScene->setFps(sceneTemplate->getFps());
    newScene->setFrameRange(sceneTemplate->getFrameRange(), false);
    if (auto *bg = newScene->getBgColorAnimator()) {
        bg->setColor(sceneTemplate->getBgColorAnimator()->getColor(
            sceneTemplate->anim_getCurrentAbsFrame()));
    }
    registerOraSceneMetadata(extractDir, newScene, rootScene, parentScene);
    emit Document::sInstance->sceneCreated(newScene);
    return newScene;
}

qsptr<BoundingBox> createOraPrecompForStack(const QDomElement &stackElement,
                                            Canvas * const sceneTemplate,
                                            const QString &extractDir,
                                            const QString &fallbackName);

void populateOraStack(const QDomElement &stackElement,
                      ContainerBox *targetGroup,
                      const QString &extractDir)
{
    if (!targetGroup) { return; }

    QList<QDomElement> children;
    for (QDomNode node = stackElement.firstChild();
         !node.isNull();
         node = node.nextSibling()) {
        const QDomElement child = node.toElement();
        if (!child.isNull()) {
            children.append(child);
        }
    }

    for (int i = children.count() - 1; i >= 0; --i) {
        const QDomElement child = children.at(i);

        const QString tag = child.tagName().toLower();
        if (tag == QStringLiteral("stack")) {
            const QString stackName = oraElementName(child);
            const QString fallbackName = stackName.isEmpty()
                    ? QObject::tr("ORA Pre-comp")
                    : stackName;
            const auto parentScene = targetGroup->getParentScene();
            const auto subLink = createOraPrecompForStack(child,
                                                          parentScene,
                                                          extractDir,
                                                          fallbackName);
            if (subLink) {
                targetGroup->addContained(subLink);
            }
            continue;
        }

        if (tag != QStringLiteral("layer")) { continue; }

        const QString src = child.attribute(QStringLiteral("src")).trimmed();
        if (src.isEmpty()) { continue; }

        const QString imagePath = QDir(extractDir).filePath(src);
        if (!QFileInfo::exists(imagePath)) { continue; }

        const auto image = enve::make_shared<ImageBox>(imagePath);
        const QString preferredName = oraElementName(child);
        targetGroup->addContained(image);
        applyOraCommonAttributes(image.get(), child);
        if (!preferredName.isEmpty()) {
            image->prp_setName(targetGroup->makeNameUniqueForContained(preferredName,
                                                                       image.get()));
        } else if (image->prp_getName().trimmed().isEmpty()) {
            image->prp_setName(QFileInfo(imagePath).completeBaseName());
        }
        image->planCenterPivotPosition();

        bool xOk = false;
        bool yOk = false;
        const qreal x = child.attribute(QStringLiteral("x"),
                                        QStringLiteral("0")).toDouble(&xOk);
        const qreal y = child.attribute(QStringLiteral("y"),
                                        QStringLiteral("0")).toDouble(&yOk);
        image->setAbsolutePos(QPointF(xOk ? x : 0.0,
                                      yOk ? y : 0.0));
    }
}

qsptr<BoundingBox> createOraPrecompForStack(const QDomElement &stackElement,
                                            Canvas * const sceneTemplate,
                                            const QString &extractDir,
                                            const QString &fallbackName)
{
    if (!sceneTemplate) {
        return nullptr;
    }

    const QString stackName = oraElementName(stackElement);
    const QString sceneName = stackName.isEmpty() ? fallbackName : stackName;
    auto *newScene = createOraSceneTemplate(sceneTemplate,
                                            sceneName,
                                            sceneTemplate->getCanvasWidth(),
                                            sceneTemplate->getCanvasHeight(),
                                            extractDir,
                                            false,
                                            sceneTemplate);
    if (!newScene) {
        return nullptr;
    }

    populateOraStack(stackElement, newScene, extractDir);
    newScene->planCenterPivotPosition();

    auto link = newScene->createLink(false);
    applyOraCommonAttributes(link.get(), stackElement);
    if (link->prp_getName().trimmed().isEmpty()) {
        link->prp_setName(sceneName);
    }
    return link;
}

qsptr<BoundingBox> importOraFileAsGroup(const QFileInfo &fileInfo)
{
    const QString extractDir = oraCacheDirForFile(fileInfo);
    extractOraArchive(fileInfo, extractDir);
    writeOraImportMetadata(fileInfo, extractDir);
    OraHotReloadManager::instance().watchImport(fileInfo, extractDir);
    const QDomDocument doc = loadOraStackDocument(extractDir);
    const QDomElement root = doc.documentElement();
    if (root.tagName().toLower() != QStringLiteral("image")) {
        RuntimeThrow(QObject::tr("ORA import failed: invalid root element."));
    }

    const auto rootGroup = enve::make_shared<ContainerBox>();
    rootGroup->prp_setName(fileInfo.completeBaseName());

    const QDomElement topStack = root.firstChildElement(QStringLiteral("stack"));
    if (topStack.isNull()) {
        RuntimeThrow(QObject::tr("ORA import failed: no layer stack found."));
    }

    applyOraCommonAttributes(rootGroup.get(), topStack);
    populateOraStack(topStack, rootGroup.get(), extractDir);
    rootGroup->planCenterPivotPosition();
    return rootGroup;
}

qsptr<BoundingBox> importOraFileAsPrecomp(const QFileInfo &fileInfo,
                                          Canvas * const scene)
{
    if (!scene || !Document::sInstance) {
        return importOraFileAsGroup(fileInfo);
    }

    const QString extractDir = oraCacheDirForFile(fileInfo);
    extractOraArchive(fileInfo, extractDir);
    writeOraImportMetadata(fileInfo, extractDir);
    OraHotReloadManager::instance().watchImport(fileInfo, extractDir);

    const QDomDocument doc = loadOraStackDocument(extractDir);
    const QDomElement root = doc.documentElement();
    if (root.tagName().toLower() != QStringLiteral("image")) {
        RuntimeThrow(QObject::tr("ORA import failed: invalid root element."));
    }

    const QDomElement topStack = root.firstChildElement(QStringLiteral("stack"));
    if (topStack.isNull()) {
        RuntimeThrow(QObject::tr("ORA import failed: no layer stack found."));
    }

    const QString baseName = fileInfo.completeBaseName().trimmed().isEmpty()
            ? QObject::tr("ORA Comp")
            : fileInfo.completeBaseName().trimmed();
    bool widthOk = false;
    bool heightOk = false;
    const int oraWidth = root.attribute(QStringLiteral("w")).toInt(&widthOk);
    const int oraHeight = root.attribute(QStringLiteral("h")).toInt(&heightOk);
    auto *newScene = createOraSceneTemplate(scene,
                                            baseName,
                                            widthOk && oraWidth > 0 ? oraWidth : scene->getCanvasWidth(),
                                            heightOk && oraHeight > 0 ? oraHeight : scene->getCanvasHeight(),
                                            extractDir,
                                            true,
                                            nullptr);
    if (!newScene) {
        return importOraFileAsGroup(fileInfo);
    }

    populateOraStack(topStack, newScene, extractDir);
    newScene->planCenterPivotPosition();

    auto link = newScene->createLink(false);
    applyOraCommonAttributes(link.get(), topStack);
    if (link->prp_getName().trimmed().isEmpty()) {
        link->prp_setName(newScene->prp_getName());
    }
    return link;
}

}


namespace OraModule {

qsptr<BoundingBox> importOraFileAsGroup(const QFileInfo &fileInfo)
{
    return ::importOraFileAsGroup(fileInfo);
}

qsptr<BoundingBox> importOraFileAsPrecomp(const QFileInfo &fileInfo,
                                          Canvas * const scene)
{
    return ::importOraFileAsPrecomp(fileInfo, scene);
}

QList<int> sceneNavigationChainIds(const Canvas *scene)
{
    if (!scene) {
        return {};
    }
    return oraSceneNavigationChainIdsForSceneId(scene->getDocumentId());
}

}
