/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file ButtonParameter.cpp
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
#include "FilterParameters/ButtonParameter.h"
#include <QDebug>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QWidget>
#include "FilterTextTranslator.h"
#include "HtmlTranslator.h"
#include "Logger.h"

namespace GmicQt
{

ButtonParameter::ButtonParameter(QObject * parent) : AbstractParameter(parent), _value(false), _alignment(Qt::AlignHCenter) {}

ButtonParameter::~ButtonParameter()
{
  // Deleting a null (or already destroyed, thanks to QPointer) widget is harmless.
  delete _pushButton.data();
}

int ButtonParameter::size() const
{
  return 1;
}

bool ButtonParameter::addTo(QWidget * widget, int row)
{
  _grid = widget ? dynamic_cast<QGridLayout *>(widget->layout()) : nullptr;
  Q_ASSERT_X(_grid, Q_FUNC_INFO, "No grid layout in widget");
  if (!_grid) { // Without this test, a release build would dereference a null pointer below
    return false;
  }
  _row = row;
  delete _pushButton.data();
  // A single '&' would be interpreted as a keyboard mnemonic marker (and would not be displayed)
  QString label = _text;
  label.replace("&", "&&");
  QPushButton * button = new QPushButton(label, widget);
  _pushButton = button;
  button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  _grid->addWidget(button, row, 0, 1, 3, _alignment);
  connectButton();
  return true;
}

QString ButtonParameter::value() const
{
  return _value ? QString("1") : QString("0");
}

QString ButtonParameter::defaultValue() const
{
  return QString("0");
}

void ButtonParameter::setValue(const QString & s)
{
  _value = (s == "1");
}

void ButtonParameter::clear()
{
  _value = false;
}

void ButtonParameter::reset() {}

void ButtonParameter::randomize()
{
  if (acceptRandom()) {
    _value = QRandomGenerator::global()->bounded(0, 2);
  }
}

void ButtonParameter::onPushButtonClicked(bool /* checked */)
{
  _value = true;
  notifyIfRelevant();
}

void ButtonParameter::connectButton()
{
  if (_pushButton) {
    connect(_pushButton, &QPushButton::clicked, this, &ButtonParameter::onPushButtonClicked);
  }
}

void ButtonParameter::disconnectButton()
{
  if (_pushButton) {
    _pushButton->disconnect(this);
  }
}

bool ButtonParameter::initFromText(const QString & filterName, const char * text, int & textLength)
{
  QList<QString> list = parseText("button", text, textLength);
  if (list.size() < 2) {
    return false;
  }
  _text = HtmlTranslator::html2txt(FilterTextTranslator::translate(list[0], filterName));
  const QString alignment = list[1].trimmed();
  if (alignment.isEmpty()) {
    return true;
  }
  // toFloat() returns 0 for a text which is not a number: it used to be taken as "left aligned".
  bool ok = false;
  const float a = alignment.toFloat(&ok);
  if (!ok) {
    Logger::warning(QString("Button parameter [%1] in filter [%2]: invalid alignment \"%3\", centered").arg(_text, filterName, alignment));
    _alignment = Qt::AlignHCenter;
    return true;
  }
  if (a == 0.0f) {
    _alignment = Qt::AlignLeft;
  } else if (a == 1.0f) {
    _alignment = Qt::AlignRight;
  } else {
    _alignment = Qt::AlignCenter;
  }
  return true;
}

} // namespace GmicQt



