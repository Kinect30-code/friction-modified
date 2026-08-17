/*
 * SPDX-FileCopyrightText: 2019-2026 Mattia Basaglia <dev@dragon.best>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "gradient_xml.hpp"

#include <QDomElement>
#include <QDomNodeList>


namespace {
// Minimal replacement for glaxnimate's svg::detail::ElementRange:
// iterates over element children (optionally filtered by tag name).
class ElementRange {
public:
    ElementRange(const QDomElement& parent, const QString& tag = QString())
        : mTag(tag)
    {
        const QDomNodeList nodes = parent.childNodes();
        for (int i = 0; i < nodes.size(); ++i) {
            const QDomElement e = nodes.at(i).toElement();
            if (e.isNull()) { continue; }
            if (mTag.isEmpty() || e.tagName() == mTag) { mItems.push_back(e); }
        }
        mIt = mItems.constBegin();
    }
    QDomElement operator*() const { return *mIt; }
    ElementRange& operator++() { ++mIt; return *this; }
    bool operator!=(const ElementRange&) const { return mIt != mItems.constEnd(); }
    ElementRange begin() const { return *this; }
    ElementRange end() const { ElementRange e = *this; e.mIt = mItems.constEnd(); return e; }
private:
    QString mTag;
    QList<QDomElement> mItems;
    QList<QDomElement>::const_iterator mIt;
};
}

using namespace glaxnimate::io;
using namespace glaxnimate::io::aep;

CosValue aep::xml_value(const QDomElement& element)
{
    if ( element.tagName() == "prop.map" )
        return xml_value(element.firstChildElement());
    else if ( element.tagName() == "prop.list" )
        return xml_list(element);
    else if ( element.tagName() == "array" )
        return xml_array(element);
    else if ( element.tagName() == "int" )
        return element.text().toDouble();
    else if ( element.tagName() == "float" )
        return element.text().toDouble();
    else if ( element.tagName() == "string" )
        return element.text();
    else
        return {};
}

CosArray aep::xml_array(const QDomElement& element)
{
    auto data = std::make_unique<CosArray::element_type>();

    for ( const auto& child : ElementRange(element) )
    {
        if ( child.tagName() != "array.type" )
            data->push_back(xml_value(child));
    }
    return data;
}

CosObject aep::xml_list(const QDomElement& element)
{
    auto data = std::make_unique<CosObject::element_type>();
    for ( const auto& pair : ElementRange(element, "prop.pair") )
    {
        QString key;
        CosValue value;
        for ( const auto& ch : ElementRange(pair) )
        {
            if ( ch.tagName() == "key" )
                key = ch.text();
            else
                value = xml_value(ch);
        }
        data->emplace(key, std::move(value));
    }

    return data;
}

Gradient aep::parse_gradient_xml(const CosValue& value)
{
    Gradient gradient;
    auto& data = get(value, "Gradient Color Data");
    gradient.color_stops = get_gradient_stops<GradientStopColor>(data);
    gradient.alpha_stops = get_gradient_stops<GradientStopAlpha>(data);
    return gradient;
}

Gradient aep::parse_gradient_xml(const QString& xml)
{
    QDomDocument dom;
    dom.setContent(xml.trimmed());
    return parse_gradient_xml(xml_value(dom.documentElement()));
}
