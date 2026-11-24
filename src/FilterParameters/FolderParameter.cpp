/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file FolderParameter.cpp
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
#include "FilterParameters/FolderParameter.h"
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <algorithm>
#include "Common.h"
#include "FilterTextTranslator.h"
#include "HtmlTranslator.h"
#include "IconLoader.h"
#include "Settings.h"

namespace GmicQt
{

FolderParameter::FolderParameter(QObject * parent) : AbstractParameter(parent), _label(nullptr), _button(nullptr) {}

FolderParameter::~FolderParameter()
{
  delete _label.data();
  delete _button.data();
}

int FolderParameter::size() const
{
  return 1;
}

bool FolderParameter::addTo(QWidget * widget, int row)
{
  _grid = widget ? dynamic_cast<QGridLayout *>(widget->layout()) : nullptr;
  Q_ASSERT_X(_grid, Q_FUNC_INFO, "No grid layout in widget");
  if (!_grid) { // Without this test, a release build would dereference a null pointer below
    return false;
  }
  _row = row;
  delete _label.data();
  delete _button.data();
  _label = nullptr;
  _button = nullptr;

  _button = new QPushButton(widget);
  _button->setIcon(IconLoader::load("folder"));
  _label = new QLabel(_name, widget);
  _grid->addWidget(_label.data(), row, 0, 1, 1);
  setTextSelectable(_label.data());
  _grid->addWidget(_button.data(), row, 1, 1, 2);
  setValue(_value);
  connect(_button.data(), &QPushButton::clicked, this, &FolderParameter::onButtonPressed);
  return true;
}

QString FolderParameter::value() const
{
  return _value;
}

QString FolderParameter::defaultValue() const
{
  return _default;
}

void FolderParameter::setValue(const QString & value)
{
  _value = value;
  if (_value.isEmpty()) {
    _value = Settings::FolderParameterDefaultValue;
  } else if (!QFileInfo(_value).isDir()) {
    _value = QDir::homePath();
  }
  if (_button) {
    const QDir abs(QDir(_value).absolutePath());
    const QFontMetrics fm(_button->font());
    // The width may be negative for a very narrow button
    _button->setText(fm.elidedText(abs.dirName(), Qt::ElideRight, std::max(0, _button->contentsRect().width() - 10)));
  }
}

void FolderParameter::reset()
{
  setValue(_default);
}

bool FolderParameter::initFromText(const QString & filterName, const char * text, int & textLength)
{
  QList<QString> list = parseText("folder", text, textLength);
  if (list.size() < 2) {
    return false;
  }
  _name = HtmlTranslator::html2txt(FilterTextTranslator::translate(list[0], filterName));
  // Surrounding double quotes are removed (no regular expression needed, and none is built for each call)
  QString defaultValue = list[1];
  if ((defaultValue.size() >= 2) && defaultValue.startsWith(QChar('"')) && defaultValue.endsWith(QChar('"'))) {
    defaultValue = defaultValue.mid(1, defaultValue.size() - 2);
  }
  if (defaultValue.isEmpty()) {
    _default.clear();
    _value = Settings::FolderParameterDefaultValue;
  } else {
    _default = _value = defaultValue;
  }
  return true;
}

bool FolderParameter::isQuoted() const
{
  return true;
}

void FolderParameter::onButtonPressed()
{
  const QString oldValue = _value;
  QFileDialog::Options options = Settings::nativeFileDialogs() ? QFileDialog::Options() : QFileDialog::DontUseNativeDialog;
  options |= QFileDialog::ShowDirsOnly;
  // The dialog is attached to the window which contains the button
  QWidget * dialogParent = _button ? _button->window() : dynamic_cast<QWidget *>(parent());
  const QString path = QFileDialog::getExistingDirectory(dialogParent, tr("Select a folder"), _value, options);
  if (path.isEmpty()) {
    setValue(oldValue);
  } else {
    Settings::FolderParameterDefaultValue = path;
    setValue(path);
  }
  notifyIfRelevant();
}

} // namespace GmicQt
