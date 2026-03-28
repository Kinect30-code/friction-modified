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

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#ifndef RASTEREFFECTMENUCREATOR_H
#define RASTEREFFECTMENUCREATOR_H

#include <functional>
#include <QHash>
#include <QSet>
#include "smartPointers/selfref.h"
#include "typemenu.h"

class RasterEffect;

struct CORE_EXPORT RasterEffectMenuCreator {
    template <typename T> using Func = std::function<T>;
    template <typename T> using Creator = Func<qsptr<T>()>;
    using EffectCreator = Creator<RasterEffect>;
    using EffectAdder = Func<void(const QString&, const QString&,
                                  const EffectCreator&)>;

    template <typename Target>
    static void addEffects(PropertyMenu * const menu,
                           void (Target::*addToTarget)(const qsptr<RasterEffect>&)) {
        struct EffectEntry {
            QString fName;
            QString fPath;
            EffectCreator fCreator;
        };

        const auto addSingleEntry =
        [menu, addToTarget](const QString& name, const QString& path,
                            const EffectCreator& creator) {
            const auto adder = [creator, addToTarget](Target * target) {
                (target->*addToTarget)(creator());
            };
            if(path.isEmpty()) {
                menu->addPlainAction<Target>(QIcon::fromTheme("effect"), name, adder);
            } else {
                const auto pathList = path.split('/');
                auto childMenu = menu->childMenu(pathList);
                if(!childMenu) childMenu = menu->addMenu(pathList);
                childMenu->addPlainAction<Target>(QIcon::fromTheme("effect"), name, adder);
            }
        };

        QList<EffectEntry> allEntries;
        const auto collector =
        [&allEntries](const QString& name, const QString& path,
                      const EffectCreator& creator) {
            allEntries << EffectEntry{name, path, creator};
        };
        forEveryEffect(collector);

        QHash<QString, QList<EffectEntry>> groups;
        for(const auto& entry : qAsConst(allEntries)) {
            const QString key = entry.fName.trimmed().toLower();
            groups[key] << entry;
        }

        QSet<QString> emitted;
        for(const auto& entry : qAsConst(allEntries)) {
            const QString key = entry.fName.trimmed().toLower();
            if(emitted.contains(key)) continue;
            emitted.insert(key);

            const auto group = groups.value(key);
            if(group.isEmpty()) continue;

            const auto& main = group.first();
            addSingleEntry(main.fName, main.fPath, main.fCreator);

            if(group.count() <= 1) continue;

            const QString variantsPath = main.fName + "/Variants";
            for(int i = 0; i < group.count(); ++i) {
                const auto& variant = group.at(i);
                QString variantName;
                if(i == 0) variantName = "Default";
                else variantName = "Alternative " + QString::number(i + 1);
                if(!variant.fPath.isEmpty()) {
                    variantName += " [" + variant.fPath + "]";
                }
                addSingleEntry(variantName, variantsPath, variant.fCreator);
            }
        }
    }
    static void forEveryEffect(const EffectAdder& add);
    static void forEveryEffectCore(const EffectAdder& add);
    static void forEveryEffectCustom(const EffectAdder& add);
    static void forEveryEffectShader(const EffectAdder& add);
};

#endif // RASTEREFFECTMENUCREATOR_H
