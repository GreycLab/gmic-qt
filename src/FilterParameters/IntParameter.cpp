/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file IntParameter.cpp
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
#include "FilterParameters/IntParameter.h"
#include <QGridLayout>
#include <QLabel>
#include <QPalette>
#include <QRandomGenerator>
#include <QSlider>
#include <QTimerEvent>
#include <QWidget>
#include <algorithm>
#include <limits>
#include "FilterParameters/CustomSpinBox.h"
#include "FilterTextTranslator.h"
#include "Globals.h"
#include "HtmlTranslator.h"
#include "Logger.h"
#include "Settings.h"

namespace GmicQt
{

IntParameter::IntParameter(QObject * parent) : AbstractParameter(parent), _min(0), _max(0), _default(0), _value(0), _label(nullptr), _slider(nullptr), _spinBox(nullptr)
{
  _timerId = 0;
  _connected = false;
}

IntParameter::~IntParameter()
{
  delete _spinBox.data();
  delete _slider.data();
  delete _label.data();
}

int IntParameter::size() const
{
  return 1;
}

bool IntParameter::addTo(QWidget * widget, int row)
{
  _grid = widget ? dynamic_cast<QGridLayout *>(widget->layout()) : nullptr;
  Q_ASSERT_X(_grid, Q_FUNC_INFO, "No grid layout in widget");
  if (!_grid) { // Without this test, a release build would dereference a null pointer below
    return false;
  }
  _row = row;
  if (_timerId) { // A pending notification must not refer to the widgets that are about to be replaced
    killTimer(_timerId);
    _timerId = 0;
  }
  delete _spinBox.data();
  delete _slider.data();
  delete _label.data();
  _spinBox = nullptr;
  _slider = nullptr;
  _label = nullptr;
  // The new widgets are not connected yet (a stale value would make connectSliderSpinBox() return early).
  _connected = false;

  _slider = new QSlider(Qt::Horizontal, widget);
  _slider->setMinimumWidth(SLIDER_MIN_WIDTH);
  _slider->setRange(_min, _max);
  _slider->setValue(_value);

  // 64 bits arithmetic: 1 + _max - _min overflows an int for wide ranges (e.g. int(0,-2000000000,2000000000))
  const qint64 delta = 1ll + _max - _min;
  if (delta < 20) {
    _slider->setPageStep(1);
  } else {
    const qint64 fact = delta < 100 ? 10 : delta < 1000 ? 100 : delta < 10000 ? 1000 : 10000;
    const qint64 pageStep = fact * (delta / fact) / 10;
    _slider->setPageStep(static_cast<int>(std::max<qint64>(1, std::min<qint64>(pageStep, std::numeric_limits<int>::max()))));
  }

  _spinBox = new CustomSpinBox(widget, _min, _max);
  _spinBox->setValue(_value);
  if (Settings::darkThemeEnabled()) {
    QPalette p = _slider->palette();
    p.setColor(QPalette::Button, QColor(100, 100, 100));
    p.setColor(QPalette::Highlight, QColor(130, 130, 130));
    _slider->setPalette(p);
  }
  _label = new QLabel(_name, widget);
  _grid->addWidget(_label.data(), row, 0, 1, 1);
  setTextSelectable(_label.data());
  _grid->addWidget(_slider.data(), row, 1, 1, 1);
  _grid->addWidget(_spinBox.data(), row, 2, 1, 1);
  connectSliderSpinBox();
  connect(_spinBox.data(), &CustomSpinBox::editingFinished, this, [this]() { notifyIfRelevant(); });

  return true;
}

QString IntParameter::value() const
{
  // The widget does not exist until addTo() has been called
  return _spinBox ? _spinBox->text() : QString::number(_value);
}

QString IntParameter::defaultValue() const
{
  return QString::number(_default);
}

void IntParameter::setValue(const QString & value)
{
  bool ok = false;
  const int k = value.toInt(&ok);
  if (!ok) {
    Logger::warning(QString("IntParameter::setValue(\"%1\"): bad value").arg(value));
    return;
  }
  // Keep the stored value consistent with what the widgets display (they clamp it themselves).
  _value = std::min(std::max(k, _min), _max);
  if (_spinBox && _slider) {
    disconnectSliderSpinBox();
    _spinBox->setValue(_value);
    _slider->setValue(_value);
    connectSliderSpinBox();
  }
}

void IntParameter::reset()
{
  _value = _default;
  if (_spinBox && _slider) {
    disconnectSliderSpinBox();
    _slider->setValue(_default);
    _spinBox->setValue(_default);
    connectSliderSpinBox();
  }
}

void IntParameter::randomize()
{
  if (!acceptRandom() || (_min > _max)) {
    return;
  }
  // QRandomGenerator::bounded() requires highest > lowest, and _max + 1 overflows an int when _max is INT_MAX.
  // Qt5 only provides bounded(int,int) and bounded(quint32,quint32) (a call with two qint64 is ambiguous),
  // so the number of values is computed in 64 bits and the generator is called with ints when it fits.
  QRandomGenerator * generator = QRandomGenerator::global();
  const qint64 count = static_cast<qint64>(_max) - static_cast<qint64>(_min) + 1; // From 1 to 2^32
  if (count <= static_cast<qint64>(std::numeric_limits<int>::max())) {
    _value = _min + generator->bounded(0, static_cast<int>(count));
  } else {
    // The range is wider than an int: the modulo bias of a 64 bits value is negligible here
    _value = static_cast<int>(static_cast<qint64>(_min) + static_cast<qint64>(generator->generate64() % static_cast<quint64>(count)));
  }
  if (_spinBox && _slider) {
    disconnectSliderSpinBox();
    _slider->setValue(_value);
    _spinBox->setValue(_value);
    connectSliderSpinBox();
  }
}

bool IntParameter::initFromText(const QString & filterName, const char * text, int & textLength)
{
  QList<QString> list = parseText("int", text, textLength);
  if (list.size() < 2) {
    return false;
  }
  _name = HtmlTranslator::html2txt(FilterTextTranslator::translate(list[0], filterName));

  QList<QString> values = list[1].split(QChar(','));
  if (values.size() != 3) {
    return false;
  }
  bool ok1 = false;
  bool ok2 = false;
  bool ok3 = false;
  _default = values[0].toInt(&ok1);
  _min = values[1].toInt(&ok2);
  _max = values[2].toInt(&ok3);
  if (!(ok1 && ok2 && ok3)) {
    return false;
  }
  // The definition comes from a filter source, which may be wrong: the widgets (and the random
  // generator) need min <= default <= max.
  if (_min > _max) {
    Logger::warning(QString("Int parameter [%1] in filter [%2]: min (%3) is greater than max (%4), values swapped").arg(_name, filterName).arg(_min).arg(_max));
    std::swap(_min, _max);
  }
  if ((_default < _min) || (_default > _max)) {
    Logger::warning(QString("Int parameter [%1] in filter [%2]: default value %3 is out of range [%4,%5]").arg(_name, filterName).arg(_default).arg(_min).arg(_max));
    _default = std::min(std::max(_default, _min), _max);
  }
  _value = _default;
  return true;
}

void IntParameter::timerEvent(QTimerEvent * e)
{
  killTimer(e->timerId());
  _timerId = 0;
  if (!_spinBox || !_spinBox->unfinishedKeyboardEditing()) {
    notifyIfRelevant();
  }
}

void IntParameter::onSliderMoved(int value)
{
  if (_spinBox && (value != _value)) {
    _spinBox->setValue(_value = value);
  }
}

void IntParameter::onSliderValueChanged(int value)
{
  if (_spinBox && (value != _value)) {
    _spinBox->setValue(_value = value);
  }
}

void IntParameter::onSpinBoxChanged(int i)
{
  _value = i;
  if (_slider) {
    _slider->setValue(i);
  }
  if (_timerId) {
    killTimer(_timerId);
    _timerId = 0;
  }
  if (!_spinBox || !_spinBox->unfinishedKeyboardEditing()) {
    _timerId = startTimer(UPDATE_DELAY);
  }
}

void IntParameter::connectSliderSpinBox()
{
  if (_connected || !_slider || !_spinBox) {
    return;
  }
  connect(_slider.data(), &QSlider::sliderMoved, this, &IntParameter::onSliderMoved);
  connect(_slider.data(), &QSlider::valueChanged, this, &IntParameter::onSliderValueChanged);
  connect(_spinBox.data(), QOverload<int>::of(&CustomSpinBox::valueChanged), this, &IntParameter::onSpinBoxChanged);
  _connected = true;
}

void IntParameter::disconnectSliderSpinBox()
{
  if (!_connected) {
    return;
  }
  if (_slider) {
    _slider->disconnect(this);
  }
  if (_spinBox) {
    _spinBox->disconnect(this);
  }
  _connected = false;
}

} // namespace GmicQt
