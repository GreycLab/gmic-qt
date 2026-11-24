/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file ChoiceParameter.cpp
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
#include "FilterParameters/ChoiceParameter.h"
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QRandomGenerator>
#include <QStringList>
#include <QWidget>
#include "Common.h"
#include "FilterTextTranslator.h"
#include "HtmlTranslator.h"
#include "Logger.h"

namespace
{

// Removes the surrounding spaces, then one leading and one trailing double quote (if any).
// (Built once per item: this used to build two QRegularExpression objects for each choice.)
QString unquotedChoice(const QString & text)
{
  QString result = text.trimmed();
  if (result.startsWith(QChar('"'))) {
    result.remove(0, 1);
  }
  if (result.endsWith(QChar('"'))) {
    result.chop(1);
  }
  return result;
}

} // namespace

namespace GmicQt
{
ChoiceParameter::ChoiceParameter(QObject * parent) : AbstractParameter(parent), _default(0), _value(0), _label(nullptr), _comboBox(nullptr), _connected(false) {}

ChoiceParameter::~ChoiceParameter()
{
  delete _comboBox.data();
  delete _label.data();
}

int ChoiceParameter::size() const
{
  return 1;
}

bool ChoiceParameter::addTo(QWidget * widget, int row)
{
  _grid = widget ? dynamic_cast<QGridLayout *>(widget->layout()) : nullptr;
  Q_ASSERT_X(_grid, Q_FUNC_INFO, "No grid layout in widget");
  if (!_grid) { // Without this test, a release build would dereference a null pointer below
    return false;
  }
  _row = row;
  delete _comboBox.data();
  delete _label.data();
  _comboBox = nullptr;
  _label = nullptr;
  // The new combo box is not connected yet. Without this, a second call to addTo() left
  // _connected set to true, so connectComboBox() returned early and the new widget never
  // notified any change.
  _connected = false;

  _comboBox = new QComboBox(widget);
  _comboBox->addItems(_choices);
  _comboBox->setCurrentIndex(_value);

  _label = new QLabel(_name, widget);
  _grid->addWidget(_label.data(), row, 0, 1, 1);
  setTextSelectable(_label.data());
  _grid->addWidget(_comboBox.data(), row, 1, 1, 2);
  connectComboBox();
  return true;
}

QString ChoiceParameter::value() const
{
  // The widget may not exist (yet), and an empty combo box has a current index of -1.
  const int index = _comboBox ? _comboBox->currentIndex() : _value;
  return QString::number(qMax(0, index));
}

QString ChoiceParameter::defaultValue() const
{
  return QString::number(_default);
}

void ChoiceParameter::setValue(const QString & value)
{
  bool ok = false;
  const int k = value.toInt(&ok);
  // The check is made against the list of choices, so that an out of range value is
  // also refused when the widget does not exist yet.
  if (!ok || (k < 0) || (k >= _choices.size())) {
    return;
  }
  _value = k;
  if (_comboBox) {
    disconnectComboBox();
    _comboBox->setCurrentIndex(_value);
    connectComboBox();
  }
}

void ChoiceParameter::reset()
{
  _value = _default;
  if (_comboBox) {
    disconnectComboBox();
    _comboBox->setCurrentIndex(_default);
    connectComboBox();
  }
}

void ChoiceParameter::randomize()
{
  const int count = _choices.size();
  // bounded(0, 0) is undefined: there must be at least one choice
  if (!acceptRandom() || (count <= 0)) {
    return;
  }
  _value = QRandomGenerator::global()->bounded(0, count);
  if (_comboBox) {
    disconnectComboBox();
    _comboBox->setCurrentIndex(_value);
    connectComboBox();
  }
}

bool ChoiceParameter::initFromText(const QString & filterName, const char * text, int & textLength)
{
  QStringList list = parseText("choice", text, textLength);
  if (list.size() < 2) {
    return false;
  }
  _name = HtmlTranslator::html2txt(FilterTextTranslator::translate(list[0], filterName));
  _choices = list[1].split(QChar(','));
  if (_choices.isEmpty()) {
    return false;
  }
  bool ok = false;
  _default = _choices.front().trimmed().toInt(&ok);
  if (!ok) {
    _default = 0;
  } else {
    _choices.pop_front();
  }
  for (QString & choice : _choices) {
    choice = HtmlTranslator::html2txt(FilterTextTranslator::translate(unquotedChoice(choice), filterName));
  }
  // The default value is an index in the list of choices: a value read from a filter definition
  // may be negative or too large, which used to leave the combo box without any selection (index -1).
  if ((_default < 0) || (_default >= _choices.size())) {
    Logger::warning(QString("Choice parameter [%1] in filter [%2]: default value %3 is out of range, 0 is used").arg(_name, filterName).arg(_default));
    _default = 0;
  }
  _value = _default;
  return true;
}

void ChoiceParameter::onComboBoxIndexChanged(int i)
{
  if (i < 0) { // The combo box has been cleared: there is no valid value to remember
    return;
  }
  _value = i;
  notifyIfRelevant();
}

void ChoiceParameter::connectComboBox()
{
  if (_connected || !_comboBox) {
    return;
  }
  connect(_comboBox.data(), QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ChoiceParameter::onComboBoxIndexChanged);
  _connected = true;
}

void ChoiceParameter::disconnectComboBox()
{
  if (!_connected || !_comboBox) {
    return;
  }
  _comboBox->disconnect(this);
  _connected = false;
}

} // namespace GmicQt
