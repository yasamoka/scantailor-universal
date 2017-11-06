/*
    Scan Tailor - Interactive post-processing tool for scanned pages.
    Copyright (C) 2007-2008  Joseph Artsimovich <joseph_a@mail.ru>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Alignment.h"
#include <QDomDocument>
#include <QDomElement>
#include <QString>

#include "CommandLine.h"

namespace page_layout
{

    
Alignment::Alignment()	
{
    *this = Alignment::load();
}
    
Alignment::Alignment(Vertical vert, Horizontal hor)	
{
    *this = Alignment::load();
    m_vert = vert;
    m_hor = hor;
}

Alignment::Alignment(Vertical vert, Horizontal hor, bool is_null, double tolerance, bool auto_marg )
    : m_vert(vert), m_hor(hor), m_isNull(is_null), m_tolerance(tolerance), m_autoMargins(auto_marg)
{
}


Alignment::Alignment(QDomElement const& el)
{
    CommandLine cli = CommandLine::get();
    m_isNull = cli.getDefaultNull(); 
    
	QString const vert(el.attribute("vert"));
	QString const hor(el.attribute("hor"));
	m_isNull = el.attribute("null").toInt() != 0;
	m_tolerance = el.attribute("tolerance", QString::number(DEFAULT_TOLERANCE)).toDouble();
	m_autoMargins = el.attribute("autoMargins") == "true" ? true: false;
	
	if (vert == "top") {
		m_vert = TOP;
	} else if (vert == "bottom") {
		m_vert = BOTTOM;
	} else if (vert == "auto") {
		m_vert = VAUTO;
	} else if (vert == "original") {
		m_vert = VORIGINAL;
	} else {
		m_vert = VCENTER;
	}
	
	if (hor == "left") {
		m_hor = LEFT;
	} else if (hor == "right") {
		m_hor = RIGHT;
	} else if (hor == "auto") {
		m_hor = HAUTO;
	} else if (vert == "original") {
		m_hor = HORIGINAL;
	} else {
		m_hor = HCENTER;
	}
}

QDomElement
Alignment::toXml(QDomDocument& doc, QString const& name) const
{
	char const* vert = 0;
	switch (m_vert) {
		case TOP:
			vert = "top";
			break;
		case VCENTER:
			vert = "vcenter";
			break;
		case BOTTOM:
			vert = "bottom";
			break;
		case VAUTO:
			vert = "auto";
			break;
		case VORIGINAL:
			vert = "original";
			break;
	}
	
	char const* hor = 0;
	switch (m_hor) {
		case LEFT:
			hor = "left";
			break;
		case HCENTER:
			hor = "hcenter";
			break;
		case RIGHT:
			hor = "right";
			break;
		case HAUTO:
			hor = "auto";
			break;
		case HORIGINAL:
			hor = "original";
			break;
	}
	
	QDomElement el(doc.createElement(name));
	el.setAttribute("vert", QLatin1String(vert));
	el.setAttribute("hor", QLatin1String(hor));
	el.setAttribute("null", m_isNull ? 1 : 0);
	el.setAttribute("tolerance", QString::number(m_tolerance));
	el.setAttribute("autoMargins", m_autoMargins ? "true" : "false");
	return el;
}

void
Alignment::save(QSettings* _settings) const
{
    std::unique_ptr<QSettings> ptr;
    if (!_settings) {
        ptr.reset(new QSettings());
        _settings = ptr.get();
    }

    QSettings& settings = *_settings;
    settings.setValue("margins/default_alignment_vert", m_vert);
    settings.setValue("margins/default_alignment_hor", m_hor);
    settings.setValue("margins/default_alignment_null", m_isNull);
    settings.setValue("margins/default_alignment_tolerance", m_tolerance);
    settings.setValue("margins/default_alignment_auto_margins", m_autoMargins);
}

Alignment
Alignment::load(QSettings* _settings)
{
    std::unique_ptr<QSettings> ptr;
    if (!_settings) {
        ptr.reset(new QSettings());
        _settings = ptr.get();
    }

    QSettings& settings = *_settings;
    Vertical vert = (Vertical) settings.value("margins/default_alignment_vert", Vertical::VCENTER).toUInt();
    Horizontal hor = (Horizontal) settings.value("margins/default_alignment_hor", Horizontal::HCENTER).toUInt();
    CommandLine cli = CommandLine::get();

    bool isnull = cli.getDefaultNull();
            //settings.value("margins/default_alignment_null", m_isNull).toBool();
    double toler = settings.value("margins/default_alignment_tolerance", DEFAULT_TOLERANCE).toDouble();
    bool auto_margins = settings.value("margins/default_alignment_auto_margins", false).toBool();
    return Alignment(vert, hor, isnull, toler, auto_margins);
}

} // namespace page_layout

