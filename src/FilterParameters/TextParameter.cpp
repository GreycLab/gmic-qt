/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file TextParameter.cpp
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
#include "FilterParameters/TextParameter.h"
#include <QAction>
#include <QDebug>
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTextEdit>
#include <QWidget>
#include "Common.h"
#include "FilterParameters/MultilineTextParameterWidget.h"
#include "FilterTextTranslator.h"
#include "HtmlTranslator.h"
#include "IconLoader.h"
#include "Misc.h"

namespace GmicQt
{

TextParameter::TextParameter(QObject * parent) : AbstractParameter(parent), _label(nullptr), _lineEdit(nullptr), _textEdit(nullptr), _multiline(false), _connected(false)
{
  _updateAction = nullptr;
}

TextParameter::~TextParameter()
{
  delete _lineEdit;
  delete _textEdit;
  delete _label;
}

int TextParameter::size() const
{
  return 1;
}

bool TextParameter::addTo(QWidget * widget, int row)
{
  _grid = widget ? dynamic_cast<QGridLayout *>(widget->layout()) : nullptr;
  Q_ASSERT_X(_grid, Q_FUNC_INFO, "No grid layout in widget");
  if (!_grid) { // Without this test, a release build would dereference a null pointer below
    return false;
  }
  _row = row;
  delete _label;
  delete _lineEdit; // Also deletes the "update" action, which is a child of the line edit
  delete _textEdit;
  _label = nullptr;
  _lineEdit = nullptr;
  _textEdit = nullptr;
  _updateAction = nullptr; // Would otherwise dangle
  _connected = false;      // The previous connections died with the previous widgets
  if (_multiline) {
    _textEdit = new MultilineTextParameterWidget(_name, _value, widget);
    _grid->addWidget(_textEdit, row, 0, 1, 3);
  } else {
    _label = new QLabel(_name, widget);
    _grid->addWidget(_label, row, 0, 1, 1);
    setTextSelectable(_label);
    _lineEdit = new QLineEdit(_value, widget);
    _grid->addWidget(_lineEdit, row, 1, 1, 2);
#if QT_VERSION_GTE(5, 2, 0)
    _updateAction = _lineEdit->addAction(IconLoader::load("view-refresh"), QLineEdit::TrailingPosition);
#endif
  }
  connectEditor();
  return true;
}

QString TextParameter::value() const
{
  // The editors do not exist until addTo() has been called
  if (_textEdit) {
    return _textEdit->text();
  }
  if (_lineEdit) {
    return _lineEdit->text();
  }
  return _value;
}

QString TextParameter::defaultValue() const
{
  return _default;
}

void TextParameter::setValue(const QString & value)
{
  _value = value;
  if (_textEdit) {
    disconnectEditor();
    _textEdit->setText(_value);
    connectEditor();
  } else if (_lineEdit) {
    disconnectEditor();
    _lineEdit->setText(_value);
    connectEditor();
  }
}

void TextParameter::setValueFromGmicStatus(const QString & value)
{
  // Same interpretation as in initFromText(): escape sequences (\n, \t, \\, \", ...) are decoded
  setValue(unescaped(value));
}

void TextParameter::reset()
{
  _value = _default;
  if (_textEdit) {
    _textEdit->setText(_default);
  } else if (_lineEdit) {
    _lineEdit->setText(_default);
  }
}

void TextParameter::randomize()
{
  if (!acceptRandom()) {
    return;
  }
  static const QString charset = QString::fromUtf8("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz01234567890      ");
  QRandomGenerator * rng = QRandomGenerator::global();
  int count = rng->bounded(5, 31);
  const int size = charset.size();
  QString text;
  text.reserve(count);
  while (count--) {
    text.append(charset[rng->bounded(size)]);
  }
  // Keep the stored value in sync (it is what value() returns while there is no editor)
  setValue(text);
}

bool TextParameter::initFromText(const QString & filterName, const char * text, int & textLength)
{
  QStringList list = parseText("text", text, textLength);
  if (list.size() < 2) {
    return false;
  }
  _name = HtmlTranslator::html2txt(FilterTextTranslator::translate(list[0], filterName));
  QString value = list[1];
  _multiline = false;
  // Built once (this function is called for each text parameter each time a filter is loaded)
  static const QRegularExpression re("^\\s*(0|1)\\s*,");
  const QRegularExpressionMatch match = re.match(value);
  if (match.hasMatch()) {
    _multiline = (match.captured(1).toInt() == 1);
    value.remove(0, match.capturedLength(0)); // The match is anchored at the beginning
  }
  _value = _default = unescaped(unquoted(value));
  return true;
}

bool TextParameter::isQuoted() const
{
  return true;
}

void TextParameter::onValueChanged()
{
  notifyIfRelevant();
}

void TextParameter::connectEditor()
{
  if (_connected) {
    return;
  }
  if (_textEdit) {
    connect(_textEdit, &MultilineTextParameterWidget::valueChanged, this, &TextParameter::onValueChanged);
  } else if (_lineEdit) {
    connect(_lineEdit, &QLineEdit::editingFinished, this, &TextParameter::onValueChanged);
#if QT_VERSION_GTE(5, 2, 0)
    if (_updateAction) {
      connect(_updateAction, &QAction::triggered, this, &TextParameter::onValueChanged);
    }
#endif
  } else {
    return; // Nothing to connect
  }
  _connected = true;
}

void TextParameter::disconnectEditor()
{
  if (!_connected) {
    return;
  }
  if (_textEdit) {
    _textEdit->disconnect(this);
  } else if (_lineEdit) {
    _lineEdit->disconnect(this);
#if QT_VERSION_GTE(5, 2, 0)
    if (_updateAction) {
      _updateAction->disconnect(this);
    }
#endif
  }
  _connected = false;
}

} // namespace GmicQt
