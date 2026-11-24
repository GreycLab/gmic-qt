/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file FileParameter.cpp
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
#include "FilterParameters/FileParameter.h"
#include <QApplication>
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
#include "FilterTextTranslator.h"
#include "HtmlTranslator.h"
#include "IconLoader.h"
#include "Settings.h"

namespace
{

// Text of the button: the name of the file, elided to fit in the given width.
// (this was written three times; a negative width, which can happen with a narrow button, is now avoided)
QString buttonTextForFile(const QString & path, const QFont & font, const int width)
{
  const QFontMetrics fm(font);
  return fm.elidedText(QFileInfo(path).fileName(), Qt::ElideRight, std::max(0, width));
}

} // namespace

namespace GmicQt
{

FileParameter::FileParameter(QObject * parent) : AbstractParameter(parent), _label(nullptr), _button(nullptr), _dialogMode(DialogMode::InputOutput) {}

FileParameter::~FileParameter()
{
  delete _label.data();
  delete _button.data();
}

int FileParameter::size() const
{
  return 1;
}

bool FileParameter::addTo(QWidget * widget, int row)
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

  const QString buttonText = _value.isEmpty() ? QString("...") : buttonTextForFile(_value, widget->font(), widget->contentsRect().width() / 3);
  _button = new QPushButton(buttonText, widget);
  _button->setIcon(IconLoader::load("document-open"));
  _label = new QLabel(_name, widget);
  _grid->addWidget(_label.data(), row, 0, 1, 1);
  setTextSelectable(_label.data());
  _grid->addWidget(_button.data(), row, 1, 1, 2);
  connect(_button.data(), &QPushButton::clicked, this, &FileParameter::onButtonPressed);
  return true;
}

QString FileParameter::value() const
{
  return _value;
}

QString FileParameter::defaultValue() const
{
  return _default;
}

void FileParameter::setValue(const QString & value)
{
  _value = value;
  if (_button) {
    if (_value.isEmpty()) {
      _button->setText("...");
    } else {
      _button->setText(buttonTextForFile(_value, _button->font(), _button->contentsRect().width() - 10));
    }
  }
}

void FileParameter::reset()
{
  setValue(_default);
}

bool FileParameter::initFromText(const QString & filterName, const char * text, int & textLength)
{
  QList<QString> list;
  if (matchType("filein", text)) {
    list = parseText("filein", text, textLength);
    _dialogMode = DialogMode::Input;
  } else if (matchType("fileout", text)) {
    list = parseText("fileout", text, textLength);
    _dialogMode = DialogMode::Output;
  } else {
    list = parseText("file", text, textLength);
    _dialogMode = DialogMode::InputOutput;
  }
  if (list.size() < 2) {
    return false;
  }
  _name = HtmlTranslator::html2txt(FilterTextTranslator::translate(list[0], filterName));
  // Surrounding double quotes are removed (no regular expression needed, and none is built for each call)
  QString defaultValue = list[1];
  if ((defaultValue.size() >= 2) && defaultValue.startsWith(QChar('"')) && defaultValue.endsWith(QChar('"'))) {
    defaultValue = defaultValue.mid(1, defaultValue.size() - 2);
  }
  _default = _value = defaultValue;
  return true;
}

bool FileParameter::isQuoted() const
{
  return true;
}

void FileParameter::onButtonPressed()
{
  QString folder;
  if (_value.isEmpty()) {
    folder = Settings::FileParameterDefaultPath;
  } else {
    folder = QFileInfo(_value).path();
  }
  if (!QFileInfo(folder).isDir()) {
    folder = QDir::homePath();
  }

  // The dialog is attached to the window which contains the button. It used to be the first of
  // QApplication::topLevelWidgets(), which is an arbitrary window and may not exist at all.
  QWidget * dialogParent = _button ? _button->window() : nullptr;

  QString filename;
  const QFileDialog::Options options = Settings::nativeFileDialogs() ? QFileDialog::Options() : QFileDialog::DontUseNativeDialog;

  switch (_dialogMode) {
  case DialogMode::Input:
    filename = QFileDialog::getOpenFileName(dialogParent, tr("Select a file"), folder, QString(), nullptr, options);
    break;
  case DialogMode::Output:
    filename = QFileDialog::getSaveFileName(dialogParent, tr("Select a file"), folder, QString(), nullptr, options);
    break;
  case DialogMode::InputOutput: {
    QFileDialog dialog(dialogParent, tr("Select a file"), folder, QString());
    dialog.setOptions(QFileDialog::DontConfirmOverwrite | options);
    dialog.setFileMode(QFileDialog::AnyFile);
    if (!_value.isEmpty()) {
      dialog.selectFile(_value);
    }
    dialog.exec();
    QStringList filenames = dialog.selectedFiles();
    if (!filenames.isEmpty() && !QFileInfo(filenames.front()).isDir()) {
      filename = filenames.front();
    }
  } break;
  }

  if (filename.isEmpty()) {
    _value.clear();
    if (_button) {
      _button->setText("...");
    }
  } else {
    _value = filename;
    Settings::FileParameterDefaultPath = QFileInfo(filename).path();
    if (_button) {
      _button->setText(buttonTextForFile(_value, _button->font(), _button->contentsRect().width() - 10));
    }
  }
  notifyIfRelevant();
}

} // namespace GmicQt
