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

#include "themesupport.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QApplication>
#include <QDebug>
#include <QDesktopWidget>
#include <QIcon>
#include <QImage>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QToolButton>
#include <QUrl>

namespace {
ThemeSupport::ThemeVariant gThemeVariant = ThemeSupport::ThemeVariant::dark;

struct ThemePaletteDef {
    QColor base;
    QColor baseDark;
    QColor baseDarker;
    QColor alternate;
    QColor highlight;
    QColor highlightDark;
    QColor highlightAlt;
    QColor highlightSelected;
    QColor buttonBase;
    QColor buttonBorder;
    QColor comboBase;
    QColor timeline;
    QColor range;
    QColor rangeSelected;
    QColor object;
    QColor red;
    QColor blue;
    QColor yellow;
    QColor pink;
    QColor green;
    QColor greenDark;
    QColor orange;
    QColor text;
    QColor textMuted;
    QColor textDisabled;
};

QColor withAlpha(const QColor &color, const int alpha)
{
    QColor result = color;
    result.setAlpha(alpha);
    return result;
}

const ThemePaletteDef &currentThemePalette()
{
    static const ThemePaletteDef kDark = {
        QColor(32, 32, 32),
        QColor(28, 28, 28),
        QColor(23, 23, 23),
        QColor(39, 39, 39),
        QColor(104, 144, 206),
        QColor(53, 101, 176),
        QColor(167, 185, 222),
        QColor(150, 191, 255),
        QColor(49, 49, 59),
        QColor(65, 65, 80),
        QColor(36, 36, 53),
        QColor(44, 44, 49),
        QColor(56, 73, 101),
        QColor(87, 120, 173),
        QColor(0, 102, 255),
        QColor(199, 67, 72),
        QColor(73, 142, 209),
        QColor(209, 183, 73),
        QColor(169, 73, 209),
        QColor(73, 209, 132),
        QColor(27, 49, 39),
        QColor(255, 123, 0),
        QColor(235, 236, 239),
        QColor(183, 186, 194),
        QColor(112, 112, 113)
    };

    static const ThemePaletteDef kLight = {
        QColor(232, 236, 242),
        QColor(218, 223, 230),
        QColor(205, 212, 221),
        QColor(242, 245, 249),
        QColor(104, 144, 206),
        QColor(53, 101, 176),
        QColor(167, 185, 222),
        QColor(150, 191, 255),
        QColor(223, 229, 237),
        QColor(167, 177, 191),
        QColor(233, 238, 245),
        QColor(214, 220, 228),
        QColor(56, 73, 101),
        QColor(87, 120, 173),
        QColor(0, 102, 255),
        QColor(199, 67, 72),
        QColor(73, 142, 209),
        QColor(209, 183, 73),
        QColor(169, 73, 209),
        QColor(73, 209, 132),
        QColor(27, 49, 39),
        QColor(255, 123, 0),
        QColor(28, 31, 36),
        QColor(83, 91, 103),
        QColor(108, 116, 128)
    };

    static const ThemePaletteDef kSlate = {
        QColor(38, 41, 47),
        QColor(33, 36, 42),
        QColor(27, 30, 35),
        QColor(46, 50, 57),
        QColor(96, 166, 255),
        QColor(66, 129, 212),
        QColor(148, 188, 236),
        QColor(170, 209, 255),
        QColor(49, 54, 63),
        QColor(79, 86, 98),
        QColor(43, 48, 56),
        QColor(60, 65, 74),
        QColor(69, 94, 134),
        QColor(104, 139, 194),
        QColor(58, 153, 255),
        QColor(211, 92, 98),
        QColor(94, 167, 236),
        QColor(214, 190, 99),
        QColor(191, 107, 222),
        QColor(93, 212, 157),
        QColor(35, 72, 55),
        QColor(242, 152, 74),
        QColor(236, 239, 243),
        QColor(181, 189, 199),
        QColor(125, 132, 142)
    };

    static const ThemePaletteDef kPaper = {
        QColor(243, 244, 240),
        QColor(235, 236, 232),
        QColor(226, 228, 222),
        QColor(249, 249, 246),
        QColor(84, 118, 184),
        QColor(63, 97, 160),
        QColor(160, 184, 220),
        QColor(122, 156, 219),
        QColor(238, 239, 235),
        QColor(191, 196, 188),
        QColor(241, 242, 238),
        QColor(216, 220, 213),
        QColor(171, 189, 214),
        QColor(116, 148, 198),
        QColor(69, 127, 218),
        QColor(199, 86, 91),
        QColor(84, 141, 206),
        QColor(201, 174, 86),
        QColor(180, 101, 205),
        QColor(80, 182, 126),
        QColor(53, 95, 72),
        QColor(230, 138, 51),
        QColor(34, 37, 41),
        QColor(98, 101, 95),
        QColor(121, 123, 118)
    };

    switch (gThemeVariant) {
    case ThemeSupport::ThemeVariant::light:
        return kLight;
    case ThemeSupport::ThemeVariant::slate:
        return kSlate;
    case ThemeSupport::ThemeVariant::paper:
        return kPaper;
    case ThemeSupport::ThemeVariant::dark:
    default:
        return kDark;
    }
}

QColor themeTextColor()
{
    return currentThemePalette().text;
}

QColor themeMutedTextColor()
{
    return currentThemePalette().textMuted;
}

QString baseThemeName()
{
    return QStringLiteral("hicolor");
}

QString generatedThemeName()
{
    switch (gThemeVariant) {
    case ThemeSupport::ThemeVariant::light:
        return QStringLiteral("friction-light");
    case ThemeSupport::ThemeVariant::paper:
        return QStringLiteral("friction-paper");
    case ThemeSupport::ThemeVariant::slate:
        return QStringLiteral("friction-slate");
    case ThemeSupport::ThemeVariant::dark:
    default:
        return baseThemeName();
    }
}

QString generatedThemeCacheRoot()
{
    auto cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheRoot.isEmpty()) {
        cacheRoot = QDir::tempPath() + QStringLiteral("/friction");
    }
    return QDir(cacheRoot).filePath(QStringLiteral("theme-icons"));
}

bool isStyleSheetThemeAsset(const QString& relativePath)
{
    return relativePath.endsWith(QStringLiteral("/actions/go-up.png")) ||
           relativePath.endsWith(QStringLiteral("/actions/go-down.png")) ||
           relativePath.endsWith(QStringLiteral("/actions/go-next.png")) ||
           relativePath.endsWith(QStringLiteral("/actions/go-previous.png")) ||
           relativePath.endsWith(QStringLiteral("/friction/box-checked.png")) ||
           relativePath.endsWith(QStringLiteral("/friction/box-unchecked.png")) ||
           relativePath.endsWith(QStringLiteral("/friction/box-checked-hover.png")) ||
           relativePath.endsWith(QStringLiteral("/friction/box-unchecked-hover.png"));
}

bool ensureParentDir(const QString& filePath)
{
    return QDir().mkpath(QFileInfo(filePath).absolutePath());
}

bool copyResourceFile(const QString& srcPath,
                      const QString& dstPath)
{
    QFile src(srcPath);
    if (!src.open(QIODevice::ReadOnly)) {
        return false;
    }
    if (!ensureParentDir(dstPath)) {
        return false;
    }
    QFile dst(dstPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return dst.write(src.readAll()) >= 0;
}

bool shouldRecolorPixel(const QColor& color)
{
    const int maxChannel = qMax(color.red(), qMax(color.green(), color.blue()));
    const int minChannel = qMin(color.red(), qMin(color.green(), color.blue()));
    return maxChannel >= 150 && (maxChannel - minChannel) <= 32;
}

bool shouldRecolorImage(const QImage& image)
{
    int visiblePixels = 0;
    int candidatePixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = QColor::fromRgba(line[x]);
            if (color.alpha() <= 8) {
                continue;
            }
            ++visiblePixels;
            if (shouldRecolorPixel(color)) {
                ++candidatePixels;
            }
        }
    }
    return visiblePixels > 0 && candidatePixels * 4 >= visiblePixels * 3;
}

QImage recolorImage(const QImage& source,
                    const QColor& targetColor)
{
    QImage result = source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < result.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const QColor color = QColor::fromRgba(line[x]);
            if (color.alpha() <= 8 || !shouldRecolorPixel(color)) {
                continue;
            }
            const int maxChannel = qMax(color.red(), qMax(color.green(), color.blue()));
            const int effectiveAlpha = qRound((color.alphaF() * maxChannel / 255.0) * 255.0);
            line[x] = QColor(targetColor.red(),
                             targetColor.green(),
                             targetColor.blue(),
                             effectiveAlpha).rgba();
        }
    }
    return result;
}

QString recolorSvgContent(const QString& content,
                          const QColor& targetColor,
                          bool* changed)
{
    QString result = content;
    const QString replacement = targetColor.name(QColor::HexRgb);
    auto replaceAll = [&](const QRegularExpression& expr) {
        const QString before = result;
        result.replace(expr, replacement);
        if (changed && result != before) {
            *changed = true;
        }
    };

    replaceAll(QRegularExpression(QStringLiteral("#ffffff(?![0-9a-fA-F])"),
                                  QRegularExpression::CaseInsensitiveOption));
    replaceAll(QRegularExpression(QStringLiteral("#fff(?![0-9a-fA-F])"),
                                  QRegularExpression::CaseInsensitiveOption));
    replaceAll(QRegularExpression(QStringLiteral("rgb\\(\\s*255\\s*,\\s*255\\s*,\\s*255\\s*\\)"),
                                  QRegularExpression::CaseInsensitiveOption));
    replaceAll(QRegularExpression(QStringLiteral("(?<=[\\s:\"'=;,(])white(?=[\\s;\"')>,])"),
                                  QRegularExpression::CaseInsensitiveOption));

    return result;
}

bool writeSvgThemeFile(const QString& srcPath,
                       const QString& dstPath,
                       const QColor& targetColor,
                       const bool forceCopy)
{
    QFile src(srcPath);
    if (!src.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QString content = QString::fromUtf8(src.readAll());
    bool changed = false;
    const QString transformed = recolorSvgContent(content, targetColor, &changed);
    if (!changed && !forceCopy) {
        return false;
    }
    if (!ensureParentDir(dstPath)) {
        return false;
    }
    QFile dst(dstPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    const QByteArray bytes = (changed ? transformed : content).toUtf8();
    return dst.write(bytes) == bytes.size();
}

bool writeRasterThemeFile(const QString& srcPath,
                          const QString& dstPath,
                          const QColor& targetColor,
                          const bool forceCopy)
{
    const QImage image(srcPath);
    if (image.isNull()) {
        return false;
    }
    const bool recolor = shouldRecolorImage(image);
    if (!recolor && !forceCopy) {
        return false;
    }
    if (!ensureParentDir(dstPath)) {
        return false;
    }
    const QImage output = recolor ? recolorImage(image, targetColor) : image;
    return output.save(dstPath);
}

QString ensureGeneratedIconTheme()
{
    if (!ThemeSupport::isLightTheme()) {
        return QStringLiteral(":/icons/hicolor");
    }

    const QString themeName = generatedThemeName();
    const QString cacheRoot = generatedThemeCacheRoot();
    const QString themeDir = QDir(cacheRoot).filePath(themeName);
    const QString stampPath = QDir(themeDir).filePath(QStringLiteral(".stamp"));
    const QString stamp = QStringLiteral("foreground=%1\n").arg(themeTextColor().name(QColor::HexRgb));

    QFile stampFile(stampPath);
    if (stampFile.exists() &&
        stampFile.open(QIODevice::ReadOnly | QIODevice::Text) &&
        QString::fromUtf8(stampFile.readAll()) == stamp &&
        QFileInfo::exists(QDir(themeDir).filePath(QStringLiteral("index.theme")))) {
        return themeDir;
    }

    QDir(themeDir).removeRecursively();
    QDir().mkpath(themeDir);

    QFile indexSrc(QStringLiteral(":/icons/hicolor/index.theme"));
    if (indexSrc.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString indexContent = QString::fromUtf8(indexSrc.readAll());
        indexContent.replace(QRegularExpression(QStringLiteral("^Name=.*$"),
                                                QRegularExpression::MultilineOption),
                             QStringLiteral("Name=%1").arg(themeName));
        ensureParentDir(QDir(themeDir).filePath(QStringLiteral("index.theme")));
        QFile indexDst(QDir(themeDir).filePath(QStringLiteral("index.theme")));
        if (indexDst.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            indexDst.write(indexContent.toUtf8());
        }
    }

    QDirIterator it(QStringLiteral(":/icons/hicolor"),
                    QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString srcPath = it.next();
        const QString relativePath = srcPath.mid(QStringLiteral(":/icons/hicolor/").size());
        if (relativePath == QStringLiteral("index.theme")) {
            continue;
        }
        const QString dstPath = QDir(themeDir).filePath(relativePath);
        const bool forceCopy = isStyleSheetThemeAsset(relativePath);
        const QString suffix = QFileInfo(srcPath).suffix().toLower();

        bool saved = false;
        if (suffix == QStringLiteral("svg")) {
            saved = writeSvgThemeFile(srcPath, dstPath, themeTextColor(), forceCopy);
        } else if (suffix == QStringLiteral("png")) {
            saved = writeRasterThemeFile(srcPath, dstPath, themeTextColor(), forceCopy);
        }

        if (!saved && forceCopy) {
            copyResourceFile(srcPath, dstPath);
        }
    }

    QFile stampDst(stampPath);
    if (stampDst.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        stampDst.write(stamp.toUtf8());
    }

    return themeDir;
}

QString themeIconBaseUrl()
{
    if (!ThemeSupport::isLightTheme()) {
        return QStringLiteral(":/icons/hicolor");
    }
    return QUrl::fromLocalFile(ensureGeneratedIconTheme()).toString(QUrl::FullyEncoded);
}

QStringList themeSearchPaths()
{
    if (!ThemeSupport::isLightTheme()) {
        return QStringList() << QStringLiteral(":/icons");
    }
    ensureGeneratedIconTheme();
    return QStringList() << generatedThemeCacheRoot()
                         << QStringLiteral(":/icons");
}
}

void ThemeSupport::setThemeVariant(const ThemeVariant variant)
{
    gThemeVariant = variant;
}

ThemeSupport::ThemeVariant ThemeSupport::themeVariant()
{
    return gThemeVariant;
}

ThemeSupport::ThemeVariant ThemeSupport::themeVariantFromSetting(const int uiThemeValue)
{
    switch (uiThemeValue) {
    case 1:
        return ThemeVariant::light;
    case 2:
        return ThemeVariant::slate;
    case 3:
        return ThemeVariant::paper;
    case 0:
    default:
        return ThemeVariant::dark;
    }
}

bool ThemeSupport::isLightTheme()
{
    return gThemeVariant == ThemeVariant::light ||
           gThemeVariant == ThemeVariant::paper;
}

const QColor ThemeSupport::getQColor(int r,
                                     int g,
                                     int b,
                                     int a)
{
    return a == 255 ? QColor(r, g, b) : QColor(r, g, b, a);
}

const QColor ThemeSupport::getThemeBaseColor(int alpha)
{
    return withAlpha(currentThemePalette().base, alpha);
}

SkColor ThemeSupport::getThemeBaseSkColor(int alpha)
{
    const auto color = currentThemePalette().base;
    return SkColorSetARGB(alpha, color.red(), color.green(), color.blue());
}

const QColor ThemeSupport::getThemeBaseDarkColor(int alpha)
{
    return withAlpha(currentThemePalette().baseDark, alpha);
}

const QColor ThemeSupport::getThemeBaseDarkerColor(int alpha)
{
    return withAlpha(currentThemePalette().baseDarker, alpha);
}

const QColor ThemeSupport::getThemeAlternateColor(int alpha)
{
    return withAlpha(currentThemePalette().alternate, alpha);
}

const QColor ThemeSupport::getThemeHighlightColor(int alpha)
{
    return withAlpha(currentThemePalette().highlight, alpha);
}

const QColor ThemeSupport::getThemeHighlightDarkerColor(int alpha)
{
    return withAlpha(currentThemePalette().highlightDark, alpha);
}

const QColor ThemeSupport::getThemeHighlightAlternativeColor(int alpha)
{
    return withAlpha(currentThemePalette().highlightAlt, alpha);
}

const QColor ThemeSupport::getThemeHighlightSelectedColor(int alpha)
{
    return withAlpha(currentThemePalette().highlightSelected, alpha);
}

SkColor ThemeSupport::getThemeHighlightSkColor(int alpha)
{
    const auto color = currentThemePalette().highlight;
    return SkColorSetARGB(alpha, color.red(), color.green(), color.blue());
}

const QColor ThemeSupport::getThemeButtonBaseColor(int alpha)
{
    return withAlpha(currentThemePalette().buttonBase, alpha);
}

const QColor ThemeSupport::getThemeButtonBorderColor(int alpha)
{
    return withAlpha(currentThemePalette().buttonBorder, alpha);
}

const QColor ThemeSupport::getThemeComboBaseColor(int alpha)
{
    return withAlpha(currentThemePalette().comboBase, alpha);
}

const QColor ThemeSupport::getThemeTimelineColor(int alpha)
{
    return withAlpha(currentThemePalette().timeline, alpha);
}

const QColor ThemeSupport::getThemeRangeColor(int alpha)
{
    return withAlpha(currentThemePalette().range, alpha);
}

const QColor ThemeSupport::getThemeRangeSelectedColor(int alpha)
{
    return withAlpha(currentThemePalette().rangeSelected, alpha);
}

const QColor ThemeSupport::getThemeFrameMarkerColor(int alpha)
{
    return getThemeColorOrange(alpha);
}

const QColor ThemeSupport::getThemeObjectColor(int alpha)
{
    return withAlpha(currentThemePalette().object, alpha);
}

const QColor ThemeSupport::getThemeColorRed(int alpha)
{
    return withAlpha(currentThemePalette().red, alpha);
}

const QColor ThemeSupport::getThemeColorBlue(int alpha)
{
    return withAlpha(currentThemePalette().blue, alpha);
}

const QColor ThemeSupport::getThemeColorYellow(int alpha)
{
    return withAlpha(currentThemePalette().yellow, alpha);
}

const QColor ThemeSupport::getThemeColorPink(int alpha)
{
    return withAlpha(currentThemePalette().pink, alpha);
}

const QColor ThemeSupport::getThemeColorGreen(int alpha)
{
    return withAlpha(currentThemePalette().green, alpha);
}

const QColor ThemeSupport::getThemeColorGreenDark(int alpha)
{
    return withAlpha(currentThemePalette().greenDark, alpha);
}

const QColor ThemeSupport::getThemeColorOrange(int alpha)
{
    return withAlpha(currentThemePalette().orange, alpha);
}

const QColor ThemeSupport::getThemeColorTextDisabled(int alpha)
{
    return withAlpha(currentThemePalette().textDisabled, alpha);
}

const QPalette ThemeSupport::getDefaultPalette(const QColor &highlight)
{
    QPalette palette;
    palette.setColor(QPalette::Window, getThemeAlternateColor());
    palette.setColor(QPalette::WindowText, themeTextColor());
    palette.setColor(QPalette::Base, getThemeBaseColor());
    palette.setColor(QPalette::AlternateBase, getThemeAlternateColor());
    palette.setColor(QPalette::Link, themeTextColor());
    palette.setColor(QPalette::LinkVisited, themeTextColor());
    palette.setColor(QPalette::ToolTipText, themeTextColor());
    palette.setColor(QPalette::ToolTipBase, isLightTheme() ? QColor(245, 247, 250) : Qt::black);
    palette.setColor(QPalette::Text, themeTextColor());
    palette.setColor(QPalette::Button, getThemeBaseColor());
    palette.setColor(QPalette::ButtonText, themeTextColor());
    palette.setColor(QPalette::BrightText, themeTextColor());
    palette.setColor(QPalette::Highlight, highlight.isValid() ? highlight : getThemeHighlightColor());
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::Text, getThemeColorTextDisabled());
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, getThemeColorTextDisabled());
    palette.setColor(QPalette::Disabled, QPalette::WindowText, getThemeColorTextDisabled());
    return palette;
}

const QPalette ThemeSupport::getDarkPalette(int alpha)
{
    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, getThemeBaseColor(alpha));
    pal.setColor(QPalette::Base, getThemeBaseColor(alpha));
    pal.setColor(QPalette::Button, getThemeBaseColor(alpha));
    return pal;
}

const QPalette ThemeSupport::getDarkerPalette(int alpha)
{
    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, getThemeBaseDarkerColor(alpha));
    pal.setColor(QPalette::Base, getThemeBaseDarkerColor(alpha));
    pal.setColor(QPalette::Button, getThemeBaseDarkerColor(alpha));
    return pal;
}

const QPalette ThemeSupport::getNotSoDarkPalette(int alpha)
{
    QPalette pal = QPalette();
    pal.setColor(QPalette::Window, getThemeAlternateColor(alpha));
    pal.setColor(QPalette::Base, getThemeBaseColor(alpha));
    pal.setColor(QPalette::Button, getThemeBaseColor(alpha));
    return pal;
}

const QString ThemeSupport::getThemeStyle(int iconSize)
{
    QString css;
    QFile stylesheet(QString::fromUtf8(":/styles/friction.qss"));
    if (stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        css = stylesheet.readAll();
        stylesheet.close();
    }
    const qreal iconPixelRatio = iconSize * qApp->desktop()->devicePixelRatioF();
    return css.arg(getThemeButtonBaseColor().name(),
                   getThemeButtonBorderColor().name(),
                   getThemeBaseDarkerColor().name(),
                   getThemeHighlightColor().name(),
                   getThemeBaseColor().name(),
                   getThemeAlternateColor().name(),
                   QString::number(getIconSize(iconSize).width()),
                   getThemeColorOrange().name(),
                   getThemeRangeSelectedColor().name(),
                   QString::number(getIconSize(iconSize / 2).width()),
                   QString::number(getIconSize(qRound(iconPixelRatio)).width()),
                   QString::number(getIconSize(qRound(iconPixelRatio / 2)).width()),
                   getThemeColorTextDisabled().name(),
                   QString::number(getIconSize(iconSize).width() / 4),
                   themeIconBaseUrl(),
                   themeTextColor().name(),
                   themeMutedTextColor().name());
}

void ThemeSupport::setupTheme(const int iconSize)
{
    QIcon::setThemeSearchPaths(themeSearchPaths());
    QIcon::setThemeName(ThemeSupport::isLightTheme() ? generatedThemeName()
                                                     : baseThemeName());
    qApp->setStyle(QString::fromUtf8("fusion"));
    qApp->setPalette(getDefaultPalette());
    qApp->setStyleSheet(getThemeStyle(iconSize));
}

const QList<QSize> ThemeSupport::getAvailableIconSizes()
{
    return QIcon::fromTheme("visible").availableSizes();
}

const QSize ThemeSupport::getIconSize(const int size)
{
    QSize requestedSize(size, size);
    const auto iconSizes = getAvailableIconSizes();
    bool hasIconSize = iconSizes.contains(requestedSize);
    if (hasIconSize) { return requestedSize; }
    const auto foundIconSize = findClosestIconSize(size);
    return foundIconSize;
}

bool ThemeSupport::hasIconSize(const int size)
{
    return getAvailableIconSizes().contains(QSize(size, size));
}

const QSize ThemeSupport::findClosestIconSize(int iconSize)
{
    const auto iconSizes = getAvailableIconSizes();
    return *std::min_element(iconSizes.begin(),
                             iconSizes.end(),
                             [iconSize](const QSize& a,
                                        const QSize& b) {
        return qAbs(a.width() - iconSize) < qAbs(b.width() - iconSize);
    });
}

void ThemeSupport::setToolbarButtonStyle(const QString &name,
                                         QToolBar *bar,
                                         QAction *act)
{
    if (!bar || !act || name.simplified().isEmpty()) { return; }
    if (QWidget *widget = bar->widgetForAction(act)) {
        if (QToolButton *button = qobject_cast<QToolButton*>(widget)) {
            button->setObjectName(name);
        }
    }
}

const QColor ThemeSupport::getLightDarkColor(const QColor &color,
                                             const int &factor)
{
    const float lightness = color.lightnessF();
    if (lightness < 0.5f) {
        const QColor col = color.lighter(factor);
        const float minLightness = 0.3f;
        if (col.lightnessF() < minLightness) {
            QColor hslColor = color.toHsl();
            hslColor.setHslF(hslColor.hslHueF(),
                             hslColor.hslSaturationF(),
                             minLightness,
                             hslColor.alphaF());
            return hslColor.toRgb();
        }
        return col;
    }

    const QColor col = color.darker(factor);
    const float maxLightness = 0.7f;
    if (col.lightnessF() > maxLightness) {
        QColor hslColor = color.toHsl();
        hslColor.setHslF(hslColor.hslHueF(),
                         hslColor.hslSaturationF(),
                         maxLightness,
                         hslColor.alphaF());
        return hslColor.toRgb();
    }
    return col;
}
