/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file BoolParameter.cpp
 *
 *  Copyright 2017 Sebastien Fourey
 *
 *  This file is part of G'MIC-Qt, a generic plug-in for raster graphics
 *  editors, offering hundreds of filters thanks to the underlying G'MIC
 *  image processing framework.
 *
 *  gmic_qt is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  gmic_qt is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with gmic_qt.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include "FilterParameters/BoolParameter.h"
#include <QCheckBox>
#include <QDebug>
#include <QGridLayout>
#include <QLabel>
#include <QPalette>
#include <QRandomGenerator>
#include <QWidget>
#include "Common.h"
#include "FilterTextTranslator.h"
#include "HtmlTranslator.h"
#include "Settings.h"

namespace GmicQt
{

BoolParameter::BoolParameter(QObject * parent) //
    : AbstractParameter(parent), _default(false), _value(false), _checkBox(nullptr), _label(nullptr), _connected(false)
{
}

BoolParameter::~BoolParameter()
{
  delete _checkBox.data();
  delete _label.data();
}

int BoolParameter::size() const
{
  return 1;
}

bool BoolParameter::addTo(QWidget * widget, int row)
{
  _grid = widget ? dynamic_cast<QGridLayout *>(widget->layout()) : nullptr;
  Q_ASSERT_X(_grid, Q_FUNC_INFO, "No grid layout in widget");
  if (!_grid) { // Without this test, a release build would dereference a null pointer below
    return false;
  }
  _row = row;
  delete _checkBox.data();
  delete _label.data();
  _checkBox = nullptr;
  _label = nullptr;
  // The new check box is not connected yet. Without this, a second call to addTo() left
  // _connected set to true, so connectCheckBox() returned early and nothing was ever notified.
  _connected = false;

  _checkBox = new QCheckBox(widget);
  _checkBox->setChecked(_value);
  _label = new QLabel(_name, widget);
  if (Settings::darkThemeEnabled()) {
    QPalette p = _checkBox->palette();
    p.setColor(QPalette::Text, Settings::CheckBoxTextColor);
    p.setColor(QPalette::Base, Settings::CheckBoxBaseColor);
    _checkBox->setPalette(p);
  }
  _grid->addWidget(_label.data(), row, 0, 1, 1);
  _grid->addWidget(_checkBox.data(), row, 1, 1, 2);
  connectCheckBox();
  return true;
}

QString BoolParameter::value() const
{
  return _value ? QString("1") : QString("0");
}

QString BoolParameter::defaultValue() const
{
  return _default ? QString("1") : QString("0");
}

void BoolParameter::setValue(const QString & value)
{
  _value = (value == "1");
  if (_checkBox) {
    disconnectCheckBox();
    _checkBox->setChecked(_value);
    connectCheckBox();
  }
}

void BoolParameter::reset()
{
  // There is no check box until addTo() has been called.
  // As for the other parameters, the widget is updated silently (changing a connected check box
  // used to emit valueChanged() for each parameter which was not already at its default value).
  setValue(_default ? QString("1") : QString("0"));
}

void BoolParameter::randomize()
{
  if (acceptRandom()) {
    setValue(QRandomGenerator::global()->bounded(0, 2) ? QString("1") : QString("0"));
  }
}

void BoolParameter::onCheckBoxChanged(bool on)
{
  _value = on;
  notifyIfRelevant();
}

void BoolParameter::connectCheckBox()
{
  if (_connected || !_checkBox) {
    return;
  }
  connect(_checkBox.data(), &QCheckBox::toggled, this, &BoolParameter::onCheckBoxChanged);
  _connected = true;
}

void BoolParameter::disconnectCheckBox()
{
  if (!_connected || !_checkBox) {
    return;
  }
  _checkBox->disconnect(this);
  _connected = false;
}

bool BoolParameter::initFromText(const QString & filterName, const char * text, int & textLength)
{
  QList<QString> list = parseText("bool", text, textLength);
  if (list.size() < 2) {
    return false;
  }
  _name = HtmlTranslator::html2txt(FilterTextTranslator::translate(list[0], filterName));
  _value = _default = (list[1].startsWith("true") || list[1].startsWith("1"));
  return true;
}

} // namespace GmicQt
