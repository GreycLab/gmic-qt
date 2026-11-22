/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file FloatParameter.cpp
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
#include "FilterParameters/FloatParameter.h"
#include <QDebug>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QPalette>
#include <QSlider>
#include <QString>
#include <QTimerEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include "FilterParameters/CustomDoubleSpinBox.h"
#include "FilterTextTranslator.h"
#include "Globals.h"
#include "HtmlTranslator.h"
#include "Logger.h"
#include "Misc.h"
#include "Settings.h"

namespace GmicQt
{

FloatParameter::FloatParameter(QObject * parent) : AbstractParameter(parent) {}

FloatParameter::~FloatParameter()
{
  // Deleting a null (or already destroyed, thanks to QPointer) widget is harmless.
  delete _spinBox.data();
  delete _slider.data();
  delete _label.data();
}

int FloatParameter::size() const
{
  return 1;
}

int FloatParameter::sliderPositionFromValue(const float value) const
{
  const float range = _max - _min;
  if (!(range > 0.0f) || !std::isfinite(range)) {
    return 0; // Empty (or invalid) range: avoids a division by zero, and NaN to int conversions
  }
  const float position = SLIDER_MAX_RANGE * (value - _min) / range;
  if (!(position > 0.0f)) { // Also catches NaN
    return 0;
  }
  if (position >= static_cast<float>(SLIDER_MAX_RANGE)) {
    return SLIDER_MAX_RANGE;
  }
  return static_cast<int>(position);
}

float FloatParameter::valueFromSliderPosition(const int position) const
{
  return _min + (static_cast<float>(position) / static_cast<float>(SLIDER_MAX_RANGE)) * (_max - _min);
}

void FloatParameter::setWidgetsValue(const float value)
{
  if (!_slider || !_spinBox) {
    return;
  }
  disconnectSliderSpinBox();
  _slider->setValue(sliderPositionFromValue(value));
  _spinBox->setValue(static_cast<double>(value));
  connectSliderSpinBox();
}

bool FloatParameter::addTo(QWidget * widget, int row)
{
  _grid = widget ? dynamic_cast<QGridLayout *>(widget->layout()) : nullptr;
  Q_ASSERT_X(_grid, Q_FUNC_INFO, "No grid layout in widget");
  if (!_grid) {
    Logger::error(QString("FloatParameter::addTo(): no grid layout in widget (parameter \"%1\")").arg(_name));
    return false;
  }
  _row = row;
  if (_timerId) {
    killTimer(_timerId);
    _timerId = 0;
  }
  delete _spinBox.data();
  delete _slider.data();
  delete _label.data();
  _connected = false; // The connections died with the previous widgets, if any

  QSlider * slider = new QSlider(Qt::Horizontal, widget);
  _slider = slider;
  slider->setMinimumWidth(SLIDER_MIN_WIDTH);
  slider->setRange(0, SLIDER_MAX_RANGE);
  slider->setValue(sliderPositionFromValue(_value));
  if (Settings::darkThemeEnabled()) {
    QPalette p = slider->palette();
    p.setColor(QPalette::Button, QColor(100, 100, 100));
    p.setColor(QPalette::Highlight, QColor(130, 130, 130));
    slider->setPalette(p);
  }

  CustomDoubleSpinBox * spinBox = new CustomDoubleSpinBox(widget, _min, _max);
  _spinBox = spinBox;
  spinBox->setSingleStep(static_cast<double>(_max - _min) / 100.0);
  spinBox->setValue(static_cast<double>(_value));

  QLabel * label = new QLabel(_name, widget);
  _label = label;
  _grid->addWidget(label, row, 0, 1, 1);
  setTextSelectable(label);
  _grid->addWidget(slider, row, 1, 1, 1);
  _grid->addWidget(spinBox, row, 2, 1, 1);

  connectSliderSpinBox();

  // The spin box is the context object (and not "this"): disconnectSliderSpinBox() removes every
  // connection from the spin box to "this", but this one must remain.
  connect(spinBox, &CustomDoubleSpinBox::editingFinished, spinBox, [this]() { notifyIfRelevant(); });

  return true;
}

QString FloatParameter::value() const
{
  // "%1" (without the 'L' modifier) always uses the C locale: there is no need to temporarily
  // change the (global, and not thread-safe to modify) default locale.
  return QString("%1").arg(_spinBox ? _spinBox->value() : static_cast<double>(_value));
}

QString FloatParameter::defaultValue() const
{
  return QString("%1").arg(static_cast<double>(_default));
}

void FloatParameter::setValue(const QString & value)
{
  bool ok = true;
  const float x = value.toFloat(&ok);
  if (!ok || !std::isfinite(x)) {
    Logger::warning(QString("FloatParameter::setValue(\"%1\"): bad value").arg(value));
    return;
  }
  _value = x;
  setWidgetsValue(_value);
}

void FloatParameter::reset()
{
  _value = _default;
  setWidgetsValue(_value);
}

void FloatParameter::randomize()
{
  if (acceptRandom()) {
    _value = static_cast<float>(randomReal(_min, _max));
    setWidgetsValue(_value);
  }
}

bool FloatParameter::initFromText(const QString & filterName, const char * text, int & textLength)
{
  textLength = 0;
  QList<QString> list = parseText("float", text, textLength);
  if (list.isEmpty()) {
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
  _default = values[0].toFloat(&ok1);
  _min = values[1].toFloat(&ok2);
  _max = values[2].toFloat(&ok3);
  if (!(ok1 && ok2 && ok3)) {
    return false;
  }
  // toFloat() accepts "nan" and "inf": such values would break the widgets.
  if (!std::isfinite(_default) || !std::isfinite(_min) || !std::isfinite(_max)) {
    Logger::warning(QString("Parameter \"%1\": float values must be finite numbers.").arg(_name));
    return false;
  }
  if (_min > _max) {
    Logger::warning(QString("Parameter \"%1\": minimum value is greater than maximum value (swapped).").arg(_name));
    std::swap(_min, _max);
  }
  _value = _default;
  return true;
}

void FloatParameter::timerEvent(QTimerEvent * event)
{
  killTimer(event->timerId());
  _timerId = 0;
  if (_spinBox && !_spinBox->unfinishedKeyboardEditing()) {
    notifyIfRelevant();
  }
}

void FloatParameter::onSliderMoved(int position)
{
  const float fValue = valueFromSliderPosition(position);
  if (_spinBox && (fValue != _value)) {
    _spinBox->setValue(_value = fValue);
  }
}

void FloatParameter::onSliderValueChanged(int position)
{
  const float fValue = valueFromSliderPosition(position);
  if (_spinBox && (fValue != _value)) {
    _spinBox->setValue(_value = fValue);
  }
}

void FloatParameter::onSpinBoxChanged(double x)
{
  _value = static_cast<float>(x);
  if (!_slider || !_spinBox) {
    return;
  }
  disconnectSliderSpinBox();
  _slider->setValue(sliderPositionFromValue(_value));
  connectSliderSpinBox();
  if (_timerId) {
    killTimer(_timerId);
  }
  if (_spinBox->unfinishedKeyboardEditing()) {
    _timerId = 0;
  } else {
    _timerId = startTimer(UPDATE_DELAY);
  }
}

void FloatParameter::connectSliderSpinBox()
{
  if (_connected || !_slider || !_spinBox) {
    return;
  }
  connect(_slider.data(), &QSlider::sliderMoved, this, &FloatParameter::onSliderMoved);
  connect(_slider.data(), &QSlider::valueChanged, this, &FloatParameter::onSliderValueChanged);
  connect(_spinBox.data(), QOverload<double>::of(&CustomDoubleSpinBox::valueChanged), this, &FloatParameter::onSpinBoxChanged);
  _connected = true;
}

void FloatParameter::disconnectSliderSpinBox()
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
