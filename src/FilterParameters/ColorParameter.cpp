/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file ColorParameter.cpp
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
#include "FilterParameters/ColorParameter.h"
#include <QApplication>
#include <QColorDialog>
#include <QDebug>
#include <QFont>
#include <QFontMetrics>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QWidget>
#include <algorithm>
#include <cstdio>
#include "FilterTextTranslator.h"
#include "HtmlTranslator.h"
#include "Logger.h"
#include "Settings.h"

namespace
{

// QColor(r,g,b,a) gives an invalid color (and a Qt warning) when a component is outside [0,255].
inline int clampedChannel(const int value)
{
  return std::min(255, std::max(0, value));
}

QString colorToString(const QColor & c, const bool withAlpha)
{
  if (withAlpha) {
    return QString("%1,%2,%3,%4").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
  }
  return QString("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue());
}

} // namespace

namespace GmicQt
{

ColorParameter::ColorParameter(QObject * parent) //
    : AbstractParameter(parent),                 //
      _default(0, 0, 0, 0),                      //
      _value(_default),                          //
      _alphaChannel(false),                      //
      _label(nullptr),                           //
      _button(nullptr),                          //
      _dialog(nullptr),                          //
      _size(-1)
{
}

ColorParameter::~ColorParameter()
{
  delete _button.data();
  delete _label.data();
  delete _dialog;
}

int ColorParameter::size() const
{
  return _size;
}

bool ColorParameter::addTo(QWidget * widget, int row)
{
  _grid = widget ? dynamic_cast<QGridLayout *>(widget->layout()) : nullptr;
  Q_ASSERT_X(_grid, Q_FUNC_INFO, "No grid layout in widget");
  if (!_grid) { // Without this test, a release build would dereference a null pointer below
    return false;
  }
  _row = row;
  delete _button.data();
  delete _label.data();
  _button = nullptr;
  _label = nullptr;

  _button = new QPushButton(widget);
  _button->setText("");

  QFontMetrics fm(widget->font());
  const QRect r = fm.boundingRect("CLR");
  // A null pixmap cannot be painted on
  _pixmap = QPixmap(std::max(1, r.width()), std::max(1, r.height()));
  _pixmap.fill(Qt::transparent); // The content of a new pixmap is undefined
  _button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

  _button->setIconSize(_pixmap.size());

  updateButtonColor();

  _label = new QLabel(_name, widget);
  _grid->addWidget(_label.data(), row, 0, 1, 1);
  setTextSelectable(_label.data());
  _grid->addWidget(_button.data(), row, 1, 1, 1);
  connect(_button.data(), &QPushButton::clicked, this, &ColorParameter::onButtonPressed);
  return true;
}

QString ColorParameter::value() const
{
  return colorToString(_value, _alphaChannel);
}

QString ColorParameter::defaultValue() const
{
  return colorToString(_default, _alphaChannel);
}

void ColorParameter::setValue(const QString & value)
{
  const QStringList list = value.split(",");
  if ((list.size() != 3) && (list.size() != 4)) {
    return;
  }
  // A value with a wrong channel used to be applied anyway (with 0 for the channel): it is now ignored.
  bool ok = false;
  const int red = list[0].toInt(&ok);
  if (!ok) {
    Logger::warning(QString("ColorParameter::setValue(\"%1\"): bad red channel").arg(value));
    return;
  }
  const int green = list[1].toInt(&ok);
  if (!ok) {
    Logger::warning(QString("ColorParameter::setValue(\"%1\"): bad green channel").arg(value));
    return;
  }
  const int blue = list[2].toInt(&ok);
  if (!ok) {
    Logger::warning(QString("ColorParameter::setValue(\"%1\"): bad blue channel").arg(value));
    return;
  }
  if ((list.size() == 4) && _alphaChannel) {
    const int alpha = list[3].toInt(&ok);
    if (!ok) {
      Logger::warning(QString("ColorParameter::setValue(\"%1\"): bad alpha channel").arg(value));
      return;
    }
    _value = QColor(clampedChannel(red), clampedChannel(green), clampedChannel(blue), clampedChannel(alpha));
  } else {
    _value = QColor(clampedChannel(red), clampedChannel(green), clampedChannel(blue));
  }
  updateButtonColor();
}

void ColorParameter::reset()
{
  _value = _default;
  updateButtonColor();
}

void ColorParameter::randomize()
{
  if (acceptRandom()) {
    auto generator = QRandomGenerator::global();
    _value.setRgb(generator->bounded(0, 256), //
                  generator->bounded(0, 256), //
                  generator->bounded(0, 256), //
                  _alphaChannel ? generator->bounded(164, 256) : 255);
    updateButtonColor();
  }
}

bool ColorParameter::initFromText(const QString & filterName, const char * text, int & textLength)
{
  QList<QString> list = parseText("color", text, textLength);
  if (list.size() < 2) {
    return false;
  }
  _name = HtmlTranslator::html2txt(FilterTextTranslator::translate(list[0], filterName));

  // color(#e9cc00) and color(#e9cc00ff)
  // (the expression is built once: this function is called for each color parameter each time a filter is loaded)
  static const QRegularExpression hexadecimalColor("^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$");
  const QString trimmed = list[1].trimmed();
  if (hexadecimalColor.match(trimmed).hasMatch()) {
    _default = QColor(trimmed.left(7));
    if (trimmed.length() == 9) {
      _alphaChannel = true;
      _default.setAlpha(trimmed.right(2).toInt(nullptr, 16));
    } else {
      _alphaChannel = false;
    }
    _size = 3 + _alphaChannel;
    _value = _default;
    return true;
  }

  // color(120,100,10) and color(120,100,10,128)
  const QList<QString> channels = list[1].split(",");
  const int n = channels.size();
  if (n > 4) {
    Logger::warning(QString("Color parameter [%1] in filter [%2]: too many components (%3), extra ones are ignored").arg(_name, filterName).arg(n));
  }
  bool okR = true, okG = true, okB = true, okA = true;
  const int r = clampedChannel((n > 0) ? channels[0].toInt(&okR) : 0);
  const int g = clampedChannel((n >= 2) ? channels[1].toInt(&okG) : r);
  const int b = clampedChannel((n >= 3) ? channels[2].toInt(&okB) : (n == 1) ? r : 0);
  if (n == 4) {
    const int a = clampedChannel(channels[3].toInt(&okA));
    _default = _value = QColor(r, g, b, a);
    _alphaChannel = true;
  } else {
    _default = _value = QColor(r, g, b);
    _alphaChannel = false;
  }
  if (okR && okG && okB && okA) {
    // The size is the number of values returned by value(), which is always 3 or 4. It used to be
    // channels.size(), which is wrong for a definition with 1, 2 or more than 4 components.
    _size = 3 + _alphaChannel;
    return true;
  }
  return false;
}

void ColorParameter::onButtonPressed()
{
  QColor color = QColorDialog::getColor(_value, QApplication::activeWindow(), tr("Select color"),
                                        (Settings::nativeColorDialogs() ? QColorDialog::ColorDialogOptions() : QColorDialog::DontUseNativeDialog) |
                                            (_alphaChannel ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions()));
  if (color.isValid()) {
    _value = color;
    updateButtonColor();
    notifyIfRelevant();
  }
}

void ColorParameter::updateButtonColor()
{
  // There is no button until addTo() has been called (and a null pixmap cannot be painted on)
  if (!_button || _pixmap.isNull()) {
    return;
  }
  QPainter painter(&_pixmap);
  const QColor color(_value);
  if (_alphaChannel) {
    // Loaded once instead of each time the color changes
    static const QImage transparency(":resources/transparency.png");
    painter.drawImage(0, 0, transparency);
  }
  painter.setBrush(color);
  painter.setPen(Qt::black);
  painter.drawRect(0, 0, _pixmap.width() - 1, _pixmap.height() - 1);
  painter.end();
  _button->setIcon(_pixmap);
}

} // namespace GmicQt
