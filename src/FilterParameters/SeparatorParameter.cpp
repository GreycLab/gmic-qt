/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file SeparatorParameter.cpp
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
#include "FilterParameters/SeparatorParameter.h"
#include <QFrame>
#include <QGridLayout>
#include <QSizePolicy>
#include "Common.h"
#include "Settings.h"

namespace GmicQt
{

SeparatorParameter::SeparatorParameter(QObject * parent) : AbstractParameter(parent) {}

SeparatorParameter::~SeparatorParameter()
{
  // Deleting a null (or already destroyed, thanks to QPointer) widget is harmless.
  delete _frame.data();
}

int SeparatorParameter::size() const
{
  return 0;
}

bool SeparatorParameter::addTo(QWidget * widget, int row)
{
  _grid = widget ? dynamic_cast<QGridLayout *>(widget->layout()) : nullptr;
  Q_ASSERT_X(_grid, Q_FUNC_INFO, "No grid layout in widget");
  if (!_grid) { // Without this test, a release build would dereference a null pointer below
    return false;
  }
  _row = row;
  delete _frame.data();
  QFrame * frame = new QFrame(widget);
  _frame = frame;
  QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  sizePolicy.setHorizontalStretch(0);
  sizePolicy.setVerticalStretch(0);
  sizePolicy.setHeightForWidth(frame->sizePolicy().hasHeightForWidth());
  frame->setSizePolicy(sizePolicy);
  frame->setFrameShape(QFrame::HLine);
  frame->setFrameShadow(QFrame::Sunken);
  if (Settings::darkThemeEnabled()) {
    frame->setStyleSheet("QFrame{ border-top: 0px none #a0a0a0; border-bottom: 2px solid rgb(160,160,160);}");
  }
  _grid->addWidget(frame, row, 0, 1, 3);
  return true;
}

QString SeparatorParameter::value() const
{
  return QString();
}

QString SeparatorParameter::defaultValue() const
{
  return QString();
}

void SeparatorParameter::setValue(const QString &) {}

void SeparatorParameter::reset() {}

bool SeparatorParameter::initFromText(const QString & /* filterName */, const char * text, int & textLength)
{
  // An empty list means a parse error, which must be reported like for the other parameters.
  const QStringList list = parseText("separator", text, textLength);
  return !list.isEmpty();
}

} // namespace GmicQt



