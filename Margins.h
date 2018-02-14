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

#ifndef MARGINS_H_
#define MARGINS_H_

#include <QSettings>
#include <memory>

class Margins
{
public:
    Margins()
    {
        QSettings settings;
        m_top = settings.value("margins/default_top", 0).toDouble();
        m_bottom = settings.value("margins/default_bottom", 0).toDouble();
        m_left = settings.value("margins/default_left", 0).toDouble();
        m_right = settings.value("margins/default_right", 0).toDouble();
    }
	
	Margins(double left, double top, double right, double bottom)
	: m_top(top), m_bottom(bottom), m_left(left), m_right(right) {}
	
	double top() const { return m_top; }
	
	void setTop(double val) { m_top = val; }
	
	double bottom() const { return m_bottom; }
	
	void setBottom(double val) { m_bottom = val; }
	
	double left() const { return m_left; }
	
	void setLeft(double val) { m_left = val; }
	
	double right() const { return m_right; }
	
	void setRight(double val) { m_right = val; }

    inline bool operator == (const Margins& rhs) const {
        return ((m_top == rhs.m_top) && (m_bottom == rhs.m_bottom) &&
                (m_left == rhs.m_left) && (m_right == rhs.m_right));
    }
private:
	double m_top;
	double m_bottom;
	double m_left;
	double m_right;
};

class MarginsWithAuto: public Margins
{
public:
    MarginsWithAuto(double left, double top, double right, double bottom)
    : Margins(top, bottom, left, right) {
        setAutoMargins(QSettings().value("margins/default_auto_margins", false).toBool());
    }

    MarginsWithAuto(const Margins& m): Margins(m) {
        setAutoMargins(QSettings().value("margins/default_auto_margins", false).toBool());
    }

    MarginsWithAuto(): Margins() {
        setAutoMargins(QSettings().value("margins/default_auto_margins", false).toBool());
    }

    MarginsWithAuto & operator= ( Margins rhs )
    {
        // copy all except autoMargins state and backup data
        static_cast<Margins&>(*this) = rhs;
        return *this;
    }

    bool isAutoMarginsEnabled() const { return m_backuppedData.get() != nullptr; }

    void setAutoMargins(bool state)
    {
        if (state && !m_backuppedData.get()) {
            backupValues();
        } else if (!state && m_backuppedData.get()) {
            restoreValues();
        }
    }

    Margins* const backup() const { return m_backuppedData.get(); }
private:
    void backupValues()
    {
        if (!m_backuppedData) {
            m_backuppedData.reset(new Margins(*this));
        }
    }

    bool restoreValues()
    {
        if (m_backuppedData) {
            static_cast<Margins&>(*this) = *m_backuppedData;
            m_backuppedData.reset();
            return true;
        }
        return false;
    }
private:
    std::shared_ptr<Margins> m_backuppedData;
};

#endif
