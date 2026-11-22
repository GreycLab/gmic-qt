/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file FloatParameter.h
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
#ifndef GMIC_QT_FLOATPARAMETER_H
#define GMIC_QT_FLOATPARAMETER_H

#include <QLabel>
#include <QPointer>
#include <QSlider>
#include <QString>
#include "AbstractParameter.h"
#include "CustomDoubleSpinBox.h"

namespace GmicQt
{

class FloatParameter : public AbstractParameter {
  Q_OBJECT
public:
  explicit FloatParameter(QObject * parent);
  ~FloatParameter() override;
  virtual int size() const override;
  bool addTo(QWidget *, int row) override;
  QString value() const override;
  QString defaultValue() const override;
  void setValue(const QString & value) override;
  void reset() override;
  void randomize() override;
  bool initFromText(const QString & filterName, const char * text, int & textLength) override;

protected:
  void timerEvent(QTimerEvent * event) override;

public slots:
  void onSliderMoved(int);
  void onSliderValueChanged(int);
  void onSpinBoxChanged(double);

private:
  void connectSliderSpinBox();
  void disconnectSliderSpinBox();
  /** Slider position (in [0,SLIDER_MAX_RANGE]) which matches a value, whatever the range is. */
  int sliderPositionFromValue(float value) const;
  float valueFromSliderPosition(int position) const;
  /** Updates the slider and the spin box (when they exist) without notifying anybody. */
  void setWidgetsValue(float value);

  QString _name;
  float _min = 0.0f;
  float _max = 0.0f;
  float _default = 0.0f;
  float _value = 0.0f;
  // Guarded pointers: the widgets are children of the parameters widget, which may delete them first.
  QPointer<QLabel> _label;
  QPointer<QSlider> _slider;
  QPointer<CustomDoubleSpinBox> _spinBox;
  int _timerId = 0;
  static const int UPDATE_DELAY = 300;
  static const int SLIDER_MAX_RANGE = 1000;
  bool _connected = false;
};

} // namespace GmicQt

#endif // GMIC_QT_FLOATPARAMETER_H
