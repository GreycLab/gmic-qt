/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file PointParameter.cpp
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
#include "FilterParameters/PointParameter.h"
#include <QApplication>
#include <QColorDialog>
#include <QDebug>
#include <QFont>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSpacerItem>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "Common.h"
#include "FilterTextTranslator.h"
#include "HtmlTranslator.h"
#include "KeypointList.h"
#include "Logger.h"
#include "Misc.h"
#include "Settings.h"

namespace GmicQt
{

namespace
{

/**
 * Reads a point coordinate: either a finite number, or "nan" (in which case isNaN is set, and
 * value is left unchanged). toFloat() also accepts "inf", which must not reach the widgets.
 */
bool parseCoordinate(const QString & text, float & value, bool & isNaN)
{
  const QString trimmed = text.trimmed();
  isNaN = (trimmed.compare(QLatin1String("nan"), Qt::CaseInsensitive) == 0);
  if (isNaN) {
    return true;
  }
  bool ok = false;
  const float v = trimmed.toFloat(&ok);
  if (!ok || !std::isfinite(v)) {
    return false;
  }
  value = v;
  return true;
}

/**
 * QColor::setRed(), setGreen()... invalidate the whole color when given a value outside [0,255].
 */
int clampedChannel(const int value)
{
  return std::max(0, std::min(255, value));
}

} // namespace

int PointParameter::_defaultColorNextIndex = 0;
unsigned long PointParameter::_randomSeed = 12345;

PointParameter::PointParameter(QObject * parent) : AbstractParameter(parent)
{
  _radius = KeypointList::Keypoint::DefaultRadius;
  setRemoved(false);
}

PointParameter::~PointParameter()
{
  // Deleting a null (or already destroyed, thanks to QPointer) widget is harmless.
  delete _label.data();
  delete _rowCell.data();
}

int PointParameter::size() const
{
  return 2;
}

bool PointParameter::addTo(QWidget * widget, int row)
{
  _grid = widget ? dynamic_cast<QGridLayout *>(widget->layout()) : nullptr;
  Q_ASSERT_X(_grid, Q_FUNC_INFO, "No grid layout in widget");
  if (!_grid) {
    Logger::error(QString("PointParameter::addTo(): no grid layout in widget (parameter \"%1\")").arg(_name));
    return false;
  }
  _row = row;
  delete _label.data();
  delete _rowCell.data(); // Deletes all the widgets of the row, which are its children
  _connected = false;     // The connections died with the previous widgets, if any

  QWidget * rowCell = new QWidget(widget);
  _rowCell = rowCell;
  auto hbox = new QHBoxLayout(rowCell);
  hbox->setContentsMargins(0, 0, 0, 0);

  QLabel * colorLabel = new QLabel(rowCell);
  _colorLabel = colorLabel;
  hbox->addWidget(colorLabel);

  QFontMetrics fm(widget->font());
  QRect r = fm.boundingRect("CLR");
  colorLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  QPixmap pixmap(std::max(1, r.width()), std::max(1, r.height())); // A null pixmap cannot be painted on
  QPainter painter(&pixmap);
  painter.setBrush(QColor(_color.red(), _color.green(), _color.blue()));
  painter.setPen(Qt::black);
  painter.drawRect(0, 0, pixmap.width() - 1, pixmap.height() - 1);
  painter.end();
  colorLabel->setPixmap(pixmap);

  QLabel * labelX = new QLabel("X", rowCell);
  _labelX = labelX;
  hbox->addWidget(labelX);
  QDoubleSpinBox * spinBoxX = new QDoubleSpinBox(rowCell);
  _spinBoxX = spinBoxX;
  hbox->addWidget(spinBoxX);
  QLabel * labelY = new QLabel("Y", rowCell);
  _labelY = labelY;
  hbox->addWidget(labelY);
  QDoubleSpinBox * spinBoxY = new QDoubleSpinBox(rowCell);
  _spinBoxY = spinBoxY;
  hbox->addWidget(spinBoxY);
  if (_removable) {
    QToolButton * removeButton = new QToolButton(rowCell);
    _removeButton = removeButton;
    hbox->addWidget(removeButton);
    removeButton->setCheckable(true);
    removeButton->setChecked(_removed);
    removeButton->setIcon(Settings::RemoveIcon);
  }
  hbox->addSpacerItem(new QSpacerItem(1, 1, QSizePolicy::Expanding, QSizePolicy::Fixed));
  spinBoxX->setRange(-200.0, 300.0);
  spinBoxY->setRange(-200.0, 300.0);
  spinBoxX->setValue(_position.x());
  spinBoxY->setValue(_position.y());
  QLabel * label = new QLabel(_name, widget);
  _label = label;
  _grid->addWidget(label, row, 0, 1, 1);
  setTextSelectable(label);
  _grid->addWidget(rowCell, row, 1, 1, 2);

  setRemoved(_removed);
  connectSpinboxes();
  return true;
}

void PointParameter::addToKeypointList(KeypointList & list) const
{
  if (_removable && _removed) {
    list.add(KeypointList::Keypoint(_color, _removable, _radius, _keepOpacityWhenSelected));
  } else {
    list.add(KeypointList::Keypoint(_position.x(), _position.y(), _color, _removable, _radius, _keepOpacityWhenSelected));
  }
}

void PointParameter::extractPositionFromKeypointList(KeypointList & list)
{
  Q_ASSERT_X(!list.isEmpty(), Q_FUNC_INFO, "Keypoint list is empty");
  if (list.isEmpty()) {
    return; // front() would be undefined
  }
  const KeypointList::Keypoint & kp = list.front();
  if (!kp.isNaN()) {
    _position.setX(kp.x);
    _position.setY(kp.y);
    if (_spinBoxX && _spinBoxY) {
      disconnectSpinboxes();
      _spinBoxX->setValue(kp.x);
      _spinBoxY->setValue(kp.y);
      connectSpinboxes();
    }
  }
  list.pop_front();
}

QString PointParameter::value() const
{
  if (_removed) {
    return "nan,nan";
  }
  return QString("%1,%2").arg(_position.x()).arg(_position.y());
}

QString PointParameter::defaultValue() const
{
  return QString("%1,%2").arg(_defaultPosition.x()).arg(_defaultPosition.y());
}

void PointParameter::setValue(const QString & value)
{
  const QStringList list = value.split(",");
  if (list.size() != 2) {
    Logger::warning(QString("PointParameter::setValue(\"%1\"): bad value (parameter \"%2\")").arg(value, _name));
    return;
  }
  const QPointF previousPosition = _position;
  const bool previousRemoved = _removed;

  float x = 0.0f;
  float y = 0.0f;
  bool xNaN = false;
  bool yNaN = false;
  const bool xOk = parseCoordinate(list[0], x, xNaN);
  const bool yOk = parseCoordinate(list[1], y, yNaN);
  if (xOk && !xNaN) {
    _position.setX(x);
  }
  if (yOk && !yNaN) {
    _position.setY(y);
  }
  _removed = (_removable && xNaN && yNaN);

  if ((_position != previousPosition) || (_removed != previousRemoved)) {
    updateView();
  }
}

void PointParameter::setVisibilityState(AbstractParameter::VisibilityState state)
{
  const VisibilityState resolved = (state == VisibilityState::Unspecified) ? defaultVisibilityState() : state;
  const bool changed = !_visibilityApplied || (resolved != visibilityState());
  AbstractParameter::setVisibilityState(resolved);
  if (changed && (resolved == VisibilityState::Visible)) {
    updateView();
  }
}

void PointParameter::updateView()
{
  if (!_spinBoxX || !_spinBoxY) {
    return;
  }
  disconnectSpinboxes();
  if (_removeButton) {
    setRemoved(_removed);
    _removeButton->setChecked(_removed);
  }
  if (!_removed) {
    _spinBoxX->setValue(_position.x());
    _spinBoxY->setValue(_position.y());
  }
  connectSpinboxes();
}

void PointParameter::reset()
{
  _position = _defaultPosition;
  enableNotifications(false);
  if (_spinBoxX && _spinBoxY) {
    _spinBoxX->setValue(_defaultPosition.x());
    _spinBoxY->setValue(_defaultPosition.y());
  }
  if (_removeButton && _removable) {
    _removeButton->setChecked((_removed = _defaultRemovedStatus));
  }
  enableNotifications(true);
}

void PointParameter::randomize()
{
  if (acceptRandom()) {
    _position = QPointF(randomReal(0.0, 100.0), randomReal(0.0, 100.0));
    if (_spinBoxX && _spinBoxY) {
      disconnectSpinboxes();
      _spinBoxX->setValue(_position.x());
      _spinBoxY->setValue(_position.y());
      connectSpinboxes();
    }
  }
}

// P = point(x,y,removable{(0),1},burst{ignored},r,g,b,a{negative->keepOpacityWhenSelected},radius,widget_visible{0|(1)})
//
// The 4th parameter (burst) is still parsed, for compatibility with existing filters, but its value is ignored:
// the plug-in decides by itself when the preview can be refreshed while a point is being dragged
// (see MainWindow::onPreviewKeypointsEvent()).
bool PointParameter::initFromText(const QString & filterName, const char * text, int & textLength)
{
  QList<QString> list = parseText("point", text, textLength);
  if (list.size() < 2) {
    return false;
  }
  _name = HtmlTranslator::html2txt(FilterTextTranslator::translate(list[0], filterName));
  QList<QString> params = list[1].split(",", QT_SKIP_EMPTY_PARTS);

  bool ok = true;
  _color.setRgb(255, 255, 255, 255);
  _removable = false;
  _radius = KeypointList::Keypoint::DefaultRadius;
  _keepOpacityWhenSelected = false;

  float x = 50.0f;
  float y = 50.0f;
  _removed = false;
  bool xNaN = true;
  bool yNaN = true;

  if (!params.isEmpty()) {
    if (!parseCoordinate(params[0], x, xNaN)) {
      return false;
    }
    if (xNaN) {
      x = 50.0f;
    }
  }

  if (params.size() >= 2) {
    if (!parseCoordinate(params[1], y, yNaN)) {
      return false;
    }
    if (yNaN) {
      y = 50.0f;
    }
  }

  _defaultPosition.setX(static_cast<qreal>(x));
  _defaultPosition.setY(static_cast<qreal>(y));
  _removed = _defaultRemovedStatus = (xNaN || yNaN);

  if (params.size() >= 3) {
    int removable = params[2].toInt(&ok);
    if (!ok) {
      return false;
    }
    switch (removable) {
    case -1:
      _removable = _removed = _defaultRemovedStatus = true;
      break;
    case 0:
      _removable = _removed = false;
      break;
    case 1:
      _removable = true;
      _defaultRemovedStatus = _removed = (xNaN && yNaN);
      break;
    default:
      return false;
    }
  }

  if (params.size() >= 4) {
    params[3].toInt(&ok); // Ignored value: only checked for syntax
    if (!ok) {
      return false;
    }
  }

  if (params.size() >= 5) {
    const int red = params[4].toInt(&ok);
    if (!ok) {
      return false;
    }
    const int gray = clampedChannel(red);
    _color.setRed(gray);
    _color.setGreen(gray);
    _color.setBlue(gray);
  } else {
    pickColorFromDefaultColormap();
  }

  if (params.size() >= 6) {
    const int green = params[5].toInt(&ok);
    if (!ok) {
      return false;
    }
    _color.setGreen(clampedChannel(green));
    _color.setBlue(0);
  }

  if (params.size() >= 7) {
    const int blue = params[6].toInt(&ok);
    if (!ok) {
      return false;
    }
    _color.setBlue(clampedChannel(blue));
  }

  if (params.size() >= 8) {
    const int alpha = params[7].toInt(&ok);
    if (!ok) {
      return false;
    }
    if (params[7].trimmed().startsWith("-") || (alpha < 0)) {
      _keepOpacityWhenSelected = true;
    }
    // Computed on a wider type: std::abs(INT_MIN) is undefined
    const long long magnitude = std::llabs(static_cast<long long>(alpha));
    _color.setAlpha(static_cast<int>(std::min(magnitude, 255ll)));
  }

  if (params.size() >= 9) {
    QString s = params[8].trimmed();
    const bool isPercentage = s.endsWith("%");
    if (isPercentage) {
      s.chop(1);
    }
    const float radius = s.toFloat(&ok);
    if (!ok || !std::isfinite(radius)) {
      return false;
    }
    // Keypoint::actualRadiusFromPreviewSize() converts the radius to an int: keep it reasonable.
    const float signedRadius = isPercentage ? -radius : radius;
    _radius = std::max(-1000.0f, std::min(10000.0f, signedRadius));
  }

  _position = _defaultPosition;
  return true;
}

void PointParameter::enableNotifications(bool on)
{
  _notificationEnabled = on;
}

void PointParameter::onSpinBoxChanged()
{
  if (!_spinBoxX || !_spinBoxY) {
    return;
  }
  _position = QPointF(_spinBoxX->value(), _spinBoxY->value());
  if (_notificationEnabled) {
    notifyIfRelevant();
  }
}

void PointParameter::setRemoved(bool on)
{
  _removed = on;
  if (_spinBoxX) {
    _spinBoxX->setDisabled(on);
  }
  if (_spinBoxY) {
    _spinBoxY->setDisabled(on);
  }
  if (_labelX) {
    _labelX->setDisabled(on);
  }
  if (_labelY) {
    _labelY->setDisabled(on);
  }
  if (_removeButton) {
    _removeButton->setIcon(on ? Settings::AddIcon : Settings::RemoveIcon);
  }
}

void PointParameter::resetDefaultColorIndex()
{
  _defaultColorNextIndex = 0;
  _randomSeed = 12345;
}

void PointParameter::onRemoveButtonToggled(bool on)
{
  setRemoved(on);
  notifyIfRelevant();
}

int PointParameter::randomChannel()
{
  int value = (_randomSeed / 65536) % 256;
  _randomSeed = _randomSeed * 1103515245 + 12345;
  return value;
}

void PointParameter::connectSpinboxes()
{
  if (_connected || !_spinBoxX || !_spinBoxY) {
    return;
  }
  connect(_spinBoxX.data(), QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PointParameter::onSpinBoxChanged);
  connect(_spinBoxY.data(), QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PointParameter::onSpinBoxChanged);
  if (_removable && _removeButton) {
    connect(_removeButton.data(), &QToolButton::toggled, this, &PointParameter::onRemoveButtonToggled);
  }
  _connected = true;
}

void PointParameter::disconnectSpinboxes()
{
  if (!_connected) {
    return;
  }
  _connected = false;
  if (!_spinBoxX || !_spinBoxY) {
    return; // The widgets (and their connections) are gone
  }
  _spinBoxX->disconnect(this);
  _spinBoxY->disconnect(this);
  if (_removable && _removeButton) {
    _removeButton->disconnect(this);
  }
}

void PointParameter::pickColorFromDefaultColormap()
{
  switch (_defaultColorNextIndex) {
  case 0:
    _color.setRgb(255, 255, 255, 255);
    break;
  case 1:
    _color = Qt::red;
    break;
  case 2:
    _color = Qt::green;
    break;
  case 3:
    _color.setRgb(64, 64, 255, 255);
    break;
  case 4:
    _color = Qt::cyan;
    break;
  case 5:
    _color = Qt::magenta;
    break;
  case 6:
    _color = Qt::yellow;
    break;
  default: {
    // The evaluation order of function arguments is unspecified: using three successive
    // calls in the setRgb() arguments gave different colors depending on the compiler.
    const int red = randomChannel();
    const int green = randomChannel();
    const int blue = randomChannel();
    _color.setRgb(red, green, blue);
  }
  }
  ++_defaultColorNextIndex;
}

} // namespace GmicQt
