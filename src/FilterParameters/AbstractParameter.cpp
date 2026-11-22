/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file AbstractParameter.cpp
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
#include "FilterParameters/AbstractParameter.h"
#include <QDebug>
#include <QGridLayout>
#include <QLabel>
#include <QRegularExpression>
#include <QWidget>
#include <cstring>
#include "Common.h"
#include "FilterParameters/BoolParameter.h"
#include "FilterParameters/ButtonParameter.h"
#include "FilterParameters/ChoiceParameter.h"
#include "FilterParameters/ColorParameter.h"
#include "FilterParameters/ConstParameter.h"
#include "FilterParameters/FileParameter.h"
#include "FilterParameters/FloatParameter.h"
#include "FilterParameters/FolderParameter.h"
#include "FilterParameters/IntParameter.h"
#include "FilterParameters/LinkParameter.h"
#include "FilterParameters/NoteParameter.h"
#include "FilterParameters/PointParameter.h"
#include "FilterParameters/SeparatorParameter.h"
#include "FilterParameters/TextParameter.h"
#include "Logger.h"

namespace
{
bool isAsciiSpace(const char c)
{
  return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\v') || (c == '\f') || (c == '\r');
}

/** Case insensitive test (ASCII only); "prefix" must be in lower case. */
bool startsWithIgnoreCase(const char * text, const char * prefix)
{
  while (*prefix) {
    char c = *text;
    if (!c) {
      return false;
    }
    if ((c >= 'A') && (c <= 'Z')) {
      c = static_cast<char>(c - 'A' + 'a');
    }
    if (c != *prefix) {
      return false;
    }
    ++text;
    ++prefix;
  }
  return true;
}

/**
 * For a parameter definition such as "name = [_~]{0,2}type(...)", returns a pointer to the
 * beginning of "type", or nullptr if there is no '=' sign.
 *
 * Replaces the regular expressions "^[^=]*\\s*=\\s*[_~]{0,2}type" which were compiled again, for
 * each of the 14 types, for each parameter, and applied to a copy of the whole remaining text.
 */
const char * parameterTypeStart(const char * text)
{
  const char * p = std::strchr(text, '=');
  if (!p) {
    return nullptr;
  }
  ++p;
  while (isAsciiSpace(*p)) {
    ++p;
  }
  for (int count = 0; (count < 2) && ((*p == '_') || (*p == '~')); ++count) {
    ++p;
  }
  return p;
}
} // namespace

namespace GmicQt
{

const QStringList AbstractParameter::NoValueParameters = {"link", "note", "separator"};

AbstractParameter::AbstractParameter(QObject * parent) : QObject(parent)
{
  _update = true;
  _visibilityState = _defaultVisibilityState = VisibilityState::Visible;
  _visibilityPropagation = VisibilityPropagation::NoPropagation;
  _row = -1;
  _grid = nullptr;
  _acceptRandom = false;
}

AbstractParameter::~AbstractParameter() {}

bool AbstractParameter::isActualParameter() const
{
  return size() > 0;
}

bool AbstractParameter::isQuoted() const
{
  return false;
}

void AbstractParameter::clear()
{
  // Used to clear the value of a ButtonParameter
}

void AbstractParameter::randomize()
{
  // By default, no effect
}

void AbstractParameter::addToKeypointList(KeypointList &) const {}

void AbstractParameter::extractPositionFromKeypointList(KeypointList &) {}

AbstractParameter * AbstractParameter::createFromText(const QString & filterName, const char * text, int & length, QString & error, QObject * parent)
{
  error.clear();
  if (!text) {
    return nullptr;
  }
  AbstractParameter * result = nullptr;

  // The order matters (the first matching prefix wins), as with the former regular expressions.
  const char * const type = parameterTypeStart(text);
  if (type) {
    if (startsWithIgnoreCase(type, "int")) {
      result = new IntParameter(parent);
    } else if (startsWithIgnoreCase(type, "float")) {
      result = new FloatParameter(parent);
    } else if (startsWithIgnoreCase(type, "bool")) {
      result = new BoolParameter(parent);
    } else if (startsWithIgnoreCase(type, "choice")) {
      result = new ChoiceParameter(parent);
    } else if (startsWithIgnoreCase(type, "color")) {
      result = new ColorParameter(parent);
    } else if (startsWithIgnoreCase(type, "separator")) {
      result = new SeparatorParameter(parent);
    } else if (startsWithIgnoreCase(type, "note")) {
      result = new NoteParameter(parent);
    } else if (startsWithIgnoreCase(type, "file")) { // Also matches "filein" and "fileout"
      result = new FileParameter(parent);
    } else if (startsWithIgnoreCase(type, "folder")) {
      result = new FolderParameter(parent);
    } else if (startsWithIgnoreCase(type, "text")) {
      result = new TextParameter(parent);
    } else if (startsWithIgnoreCase(type, "link")) {
      result = new LinkParameter(parent);
    } else if (startsWithIgnoreCase(type, "value")) {
      result = new ConstParameter(parent);
    } else if (startsWithIgnoreCase(type, "button")) {
      result = new ButtonParameter(parent);
    } else if (startsWithIgnoreCase(type, "point")) {
      result = new PointParameter(parent);
    }
  }
  if (result) {
    if (!result->initFromText(filterName, text, length)) {
      delete result;
      result = nullptr;
      // Error messages only: the (potentially long) text is converted to a QString only when needed.
      const QString line = QString::fromUtf8(text);
      if (!line.isEmpty()) {
        QRegularExpression nameRegExp("^([^=]*\\s*)=");
        QRegularExpressionMatch match = nameRegExp.match(line);
        if (match.hasMatch()) {
          QString name = match.captured(1);
          error = "Parameter name: " + name + "\n" + error;
        }
      }
    }
  } else {
    const QString line = QString::fromUtf8(text);
    if (!line.isEmpty()) {
      QRegularExpression nameRegExp("^([^=]*\\s*)=");
      QRegularExpressionMatch match = nameRegExp.match(line);
      if (match.hasMatch()) {
        QString name = match.captured(1);
        QRegularExpression typeRegExp(R"_(^[^=]*\s*=\s*[_~]{0,2}([^\( ]*)\s*\()_");
        match = typeRegExp.match(line);
        if (match.hasMatch()) {
          error = "Parameter name: " + name + "\n" + "Type <" + match.captured(1) + "> is not recognized\n" + error;
        } else {
          error = "Parameter name: " + name + "\n" + error;
        }
      }
    }
  }
  return result;
}

AbstractParameter::VisibilityState AbstractParameter::defaultVisibilityState() const
{
  return _defaultVisibilityState;
}

void AbstractParameter::hideWidgets()
{
  if (!_grid || (_row == -1)) {
    return;
  }
  for (int col = 0; col < 5; ++col) {
    QLayoutItem * item = _grid->itemAtPosition(_row, col);
    QWidget * widget = item ? item->widget() : nullptr; // An item may be a spacer or a layout
    if (widget) {
      widget->hide();
    }
  }
}

void AbstractParameter::setVisibilityState(AbstractParameter::VisibilityState state)
{
  if (state == VisibilityState::Unspecified) {
    setVisibilityState(defaultVisibilityState());
    return;
  }
  _visibilityState = state;
  if (!_grid || (_row == -1)) {
    return;
  }
  for (int col = 0; col < 5; ++col) {
    QLayoutItem * item = _grid->itemAtPosition(_row, col);
    QWidget * widget = item ? item->widget() : nullptr; // An item may be a spacer or a layout
    if (widget) {
      switch (state) {
      case VisibilityState::Visible:
        widget->setEnabled(true);
        widget->show();
        break;
      case VisibilityState::Disabled:
        widget->setEnabled(false);
        widget->show();
        break;
      case VisibilityState::Hidden:
        widget->hide();
        break;
      case VisibilityState::Unspecified:
        // Taken care above (if)
        break;
      }
    }
  }
}

AbstractParameter::VisibilityState AbstractParameter::visibilityState() const
{
  return _visibilityState;
}

AbstractParameter::VisibilityPropagation AbstractParameter::visibilityPropagation() const
{
  return _visibilityPropagation;
}

bool AbstractParameter::acceptRandom() const
{
  return _acceptRandom;
}

void AbstractParameter::setTextSelectable(QLabel * label)
{
  Qt::TextInteractionFlags flags = label->textInteractionFlags();
  flags.setFlag(Qt::TextSelectableByMouse, true);
  label->setTextInteractionFlags(flags);
}

QStringList AbstractParameter::parseText(const QString & type, const char * text, int & length)
{
  QStringList result;
  const QString str = text;
  result << str.left(str.indexOf("=")).trimmed();
#ifdef _GMIC_QT_DEBUG_
  _debugName = result.back();
#endif

  QRegularExpression re(QString("^[^=]*\\s*=\\s*([_~]{0,2})%1\\s*(.)").arg(type), QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatch match = re.match(str);
  const int prefixLength = match.captured(0).toUtf8().size();

  _update = !match.captured(1).contains("_");
  _acceptRandom = match.captured(1).contains("~");

  QString open = match.captured(2);
  const char * end = nullptr;
  const char * closing = (open == "(") ? ")" : (open == "{") ? "}" : (open == "[") ? "]" : nullptr;
  if (!closing) {
    Logger::error(QString("Parse error in %1 parameter (invalid opening character '%2').").arg(type).arg(open));
    length = 1 + prefixLength;
    return QStringList();
  }
  end = strstr(text + prefixLength, closing);
  if (!end) {
    Logger::error(QString("Parse error in %1 parameter (cannot find closing '%2').").arg(type).arg(closing));
    length = 1 + prefixLength;
    return QStringList();
  }

  // QString values = str.mid(prefixLength, -1).left(end - (text + prefixLength)).trimmed();
  QString values = QString::fromUtf8(text + prefixLength, int(end - (text + prefixLength))).trimmed();
  length = int(1 + end - text);

  if (text[length] == '_' && text[length + 1] >= '0' && text[length + 1] <= '2') {
    _defaultVisibilityState = static_cast<VisibilityState>(text[length + 1] - '0');
    _visibilityPropagation = VisibilityPropagation::NoPropagation;
    switch (text[length + 2]) {
    case '-':
      _visibilityPropagation = VisibilityPropagation::Up;
      length += 3;
      break;
    case '+':
      _visibilityPropagation = VisibilityPropagation::Down;
      length += 3;
      break;
    case '*':
      _visibilityPropagation = VisibilityPropagation::Down;
      length += 3;
      break;
    default:
      length += 2;
      break;
    }
    if (NoValueParameters.contains(type)) {
      Logger::warning(QString("Warning: %1 parameter should not define visibility. Ignored.").arg(result.first()));
      _defaultVisibilityState = AbstractParameter::VisibilityState::Visible;
      _visibilityPropagation = VisibilityPropagation::NoPropagation;
    }
  }
  while (text[length] && (text[length] == ',' || QChar(text[length]).isSpace())) {
    ++length;
  }
  result << values;
  return result;
}

bool AbstractParameter::matchType(const QString & type, const char * text) const
{
  return QString::fromUtf8(text).contains(QRegularExpression(QString("^[^=]*\\s*=\\s*_?%1\\s*.").arg(type), QRegularExpression::CaseInsensitiveOption));
}

void AbstractParameter::notifyIfRelevant()
{
  if (_update) {
    emit valueChanged();
  }
}

} // namespace GmicQt


