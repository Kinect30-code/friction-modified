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

#include "expression.h"

#include "exceptions.h"
#include "Private/esettings.h"

Expression::ResultTester Expression::sQrealAnimatorTester =
        [](const QJSValue& val) {
            if(!val.isNumber()) PrettyRuntimeThrow("Invalid return type");
        };

Expression::Expression(const QString& definitionsStr,
                       const QString& scriptStr,
                       PropertyBindingMap&& bindings,
                       std::unique_ptr<QJSEngine>&& engine,
                       QJSValue&& eEvaluate) :
    mDefinitionsStr(definitionsStr),
    mScriptStr(scriptStr),
    mEEvaluate(std::move(eEvaluate)),
    mBindings(std::move(bindings)),
    mEngine(std::move(engine)) {
    for(const auto& binding : mBindings) {
        connect(binding.second.get(), &PropertyBinding::currentValueChanged,
                this, &Expression::currentValueChanged);
        connect(binding.second.get(), &PropertyBinding::relRangeChanged,
                this, &Expression::relRangeChanged);
    }
}


void throwIfError(const QJSValue& value, const QString& name) {
    if(value.isError()) {
        PrettyRuntimeThrow("Uncaught exception in " + name + " at line "
                           + value.property("lineNumber").toString() +
                           ":\n" + value.toString());
    }
}

void Expression::sAddDefinitionsTo(const QString& definitionsStr,
                                   QJSEngine& e)
{
    QString defs;
    const auto expressions = eSettings::sInstance->fExpressions.getDefinitions();
    for (const auto &expr : expressions) { defs.append(expr.definitions); }
    defs.append(definitionsStr);

    const auto defRet = e.evaluate(defs);
    throwIfError(defRet, "Definitions");
}

namespace {
QString normalizedScript(QString script)
{
    script = script.trimmed();
    if (!script.contains(QStringLiteral("return")) &&
        !script.contains(QLatin1Char(';')) &&
        !script.contains(QLatin1Char('\n'))) {
        script = QStringLiteral("return %1;").arg(script);
    }
    return script;
}

QString commonExpressionHelpers(const QStringList& bindingVars)
{
    QString helpers;
    if (!bindingVars.contains(QStringLiteral("time"))) {
        helpers += QStringLiteral(
            "var time = (typeof frame === 'number' && typeof fps === 'number' && fps !== 0) ? frame / fps : 0;\n");
    }
    if (!bindingVars.contains(QStringLiteral("sin"))) {
        helpers += QStringLiteral("var sin = Math.sin;\n");
    }
    if (!bindingVars.contains(QStringLiteral("wiggle"))) {
        helpers += QStringLiteral(
            "function _frictionExprHash(n) {\n"
            "    var x = Math.sin(n * 12.9898) * 43758.5453;\n"
            "    return (x - Math.floor(x)) * 2 - 1;\n"
            "}\n"
            "function _frictionExprNoise(x, seed) {\n"
            "    var i = Math.floor(x);\n"
            "    var f = x - i;\n"
            "    var u = f * f * (3 - 2 * f);\n"
            "    var a = _frictionExprHash(i + seed * 101.3);\n"
            "    var b = _frictionExprHash(i + 1 + seed * 101.3);\n"
            "    return a + (b - a) * u;\n"
            "}\n"
            "function wiggle(frequency, amplitude, seed, t, detail) {\n"
            "    frequency = frequency === undefined ? 1 : frequency;\n"
            "    amplitude = amplitude === undefined ? 1 : amplitude;\n"
            "    seed = seed === undefined ? 0 : seed;\n"
            "    t = t === undefined ? time : t;\n"
            "    detail = detail === undefined ? 4 : detail;\n"
            "    var total = 0;\n"
            "    var amp = amplitude;\n"
            "    var freq = frequency;\n"
            "    for (var i = 0; i < detail; i++) {\n"
            "        total += _frictionExprNoise(t * freq, seed + i) * amp;\n"
            "        freq *= 2;\n"
            "        amp *= 0.5;\n"
            "    }\n"
            "    return value + total;\n"
            "}\n");
    }
    if (!bindingVars.contains(QStringLiteral("loopOut"))) {
        helpers += QStringLiteral(
            "function loopOut(type, duration, amplitude) {\n"
            "    type = type || 'cycle';\n"
            "    duration = duration === undefined ? 1 : duration;\n"
            "    amplitude = amplitude === undefined ? 100 : amplitude;\n"
            "    if (duration === 0) { return value; }\n"
            "    var t = time % duration;\n"
            "    if (t < 0) { t += duration; }\n"
            "    if (type === 'pingpong') {\n"
            "        var pt = time % (duration * 2);\n"
            "        if (pt < 0) { pt += duration * 2; }\n"
            "        if (pt > duration) { pt = duration * 2 - pt; }\n"
            "        t = pt;\n"
            "    }\n"
            "    if (type === 'continue') { return value + time * amplitude / duration; }\n"
            "    return value + Math.sin(t * Math.PI * 2 / duration) * amplitude;\n"
            "}\n");
    }
    return helpers;
}
}

void Expression::sAddScriptTo(const QString& scriptStr,
                              const PropertyBindingMap& bindings,
                              QJSEngine& e, QJSValue& eEvaluate,
                              const ResultTester& resultTester) {
    QStringList bindingVars;
    QJSValueList testArgs;
    for(const auto& binding : bindings) {
        bindingVars << binding.first;
        testArgs << binding.second->getJSValue(e);
    }
    const QString evalVars = bindingVars.join(", ");
    eEvaluate = e.evaluate(
            "var eEvaluate;"
            "eEvaluate = function(" + evalVars + ") {" +
                commonExpressionHelpers(bindingVars) +
                normalizedScript(scriptStr) +
            "}");
    throwIfError(eEvaluate, "Script");
    if(!eEvaluate.isCallable())
        PrettyRuntimeThrow("Uncallable script.");
    const auto testResult = eEvaluate.call(testArgs);
    if(testResult.isError()) {
        PrettyRuntimeThrow("Script test error:\n" +
                           testResult.toString());
    } else if(resultTester) resultTester(testResult);
}

qsptr<Expression> Expression::sCreate(const QString& bindingsStr,
                                      const QString& definitionsStr,
                                      const QString& scriptStr,
                                      const Property* const context,
                                      const ResultTester& resultTester) {
    auto bindings = PropertyBindingParser::parseBindings(
                              bindingsStr, nullptr, context);
    auto engine = std::make_unique<QJSEngine>();
    sAddDefinitionsTo(definitionsStr, *engine);
    QJSValue eEvaluate;
    sAddScriptTo(scriptStr, bindings, *engine, eEvaluate, resultTester);
    return sCreate(definitionsStr, scriptStr,
                   std::move(bindings),
                   std::move(engine),
                   std::move(eEvaluate));
}

qsptr<Expression> Expression::sCreate(const QString& definitionsStr,
                                      const QString& scriptStr,
                                      PropertyBindingMap&& bindings,
                                      std::unique_ptr<QJSEngine>&& engine,
                                      QJSValue&& eEvaluate) {
    if(!eEvaluate.isCallable())
        RuntimeThrow("Uncallable script:\n" + scriptStr);
    return qsptr<Expression>(new Expression(definitionsStr, scriptStr,
                                            std::move(bindings),
                                            std::move(engine),
                                            std::move(eEvaluate)));
}

bool Expression::setAbsFrame(const int absFrame) {
    bool changed = false;
    for(const auto& binding : mBindings) {
        const bool c = binding.second->setAbsFrame(absFrame);
        changed = changed || c;
    }
    return changed;
}

bool Expression::isStatic() const {
    return identicalRelRange(0) == FrameRange::EMINMAX;
}

bool Expression::isValid() {
    for(const auto& binding : mBindings) {
        const bool valid = binding.second->isValid();
        if(!valid) return false;
    }
    return true;
}

bool Expression::dependsOn(const Property* const prop) {
    for(const auto& binding : mBindings) {
        const bool depends = binding.second->dependsOn(prop);
        if(depends) return true;
    }
    return false;
}

QJSValue Expression::evaluate() {
    QJSValueList values;
    for(const auto& binding : mBindings) {
        values << binding.second->getJSValue(*mEngine);
    }
    return mEEvaluate.call(values);
}

QJSValue Expression::evaluate(const qreal relFrame)
{
    QJSValueList values;
    for (const auto& binding : mBindings) {
        QString path = binding.second->path();
        QJSValue val = binding.second->getJSValue(*mEngine, relFrame);
        if (path == "$frame") { values << QJSValue(relFrame); }
        else { values << val; }
    }
    QJSValue res = mEEvaluate.call(values);
    return res;
}

FrameRange Expression::identicalRelRange(const int absFrame) const {
    FrameRange result{FrameRange::EMINMAX};
    for(const auto& binding : mBindings) {
        const auto prop = binding.second.get();
        result *= prop->identicalRelRange(absFrame);
        if(result.isUnary()) return result;
    }
    return result;
}

FrameRange Expression::nextNonUnaryIdenticalRelRange(const int absFrame) const
{
    for (int i = absFrame; i < FrameRange::EMAX; i++) {
        FrameRange result{FrameRange::EMINMAX};
        int lowestMax = INT_MAX;
        for (const auto& binding : mBindings) {
            // ok, so binding.second is bork, why? ask the original author, I don't know or care anymore :)
            // I don't see any issues with this (everything works), but it's not "good" code either :P
            Q_UNUSED(binding)
            //const auto prop = binding.second.get();
            const auto childRange = FrameRange{FrameRange::EMAX/2, FrameRange::EMAX}; //prop->nextNonUnaryIdenticalRelRange(i);
            lowestMax = qMin(lowestMax, childRange.fMax);
            result *= childRange;
        }
        if (!result.isUnary()) { return result; }
        i = lowestMax;
    }

    return FrameRange::EMINMAX;
}

QString Expression::bindingsString() const {
    QString result;
    for(const auto& binding : mBindings) {
        result += binding.first + " = " + binding.second->path() + ";\n";
    }
    return result;
}
