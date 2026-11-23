/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file LinkParameter.cpp
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
#include "FilterParameters/LinkParameter.h"
#include <QDebug>
#include <QDesktopServices>
#include <QGridLayout>
#include <QLabel>
#include <QString>
#include <QUrl>
#include "Common.h"
#include "FilterTextTranslator.h"
#include "HtmlTranslator.h"
#include "Logger.h"

namespace
{

// The URL of a link comes from a filter definition, i.e. from a source that may be downloaded from the
// Internet or supplied by the user. Only web and e-mail links are opened: other schemes (file:, smb:,
// ms-msdt:, or any scheme registered by an application) could launch arbitrary programs or documents.
// Strict parsing: invalid characters (spaces, control characters...) make the URL invalid, instead of being silently encoded.
QUrl parsedUrl(const QString & text)
{
  return QUrl(text, QUrl::StrictMode);
}

bool isAllowedLinkUrl(const QUrl & url)
{
  if (!url.isValid()) {
    return false;
  }
  const QString scheme = url.scheme(); // Always lower case
  if ((scheme == "http") || (scheme == "https")) {
    return !url.host().isEmpty();
  }
  if (scheme == "mailto") {
    return !url.path().isEmpty();
  }
  return false;
}

// Splits a comma-separated list of arguments, ignoring the commas that are inside double quotes
// (a backslash escapes the next character, inside quotes).
QList<QString> splitArguments(const QString & text)
{
  QList<QString> result;
  QString current;
  bool inQuotes = false;
  for (int i = 0; i < text.size(); ++i) {
    const QChar c = text.at(i);
    if (inQuotes && (c == QChar('\\')) && (i + 1 < text.size())) {
      current.append(c);
      current.append(text.at(++i));
      continue;
    }
    if (c == QChar('"')) {
      inQuotes = !inQuotes;
    } else if ((c == QChar(',')) && !inQuotes) {
      result.push_back(current);
      current.clear();
      continue;
    }
    current.append(c);
  }
  result.push_back(current);
  return result;
}

// Removes the leading and trailing spaces, then a leading and a trailing double quote (if any).
QString unquoted(const QString & text)
{
  QString result = text.trimmed();
  if (result.startsWith(QChar('"'))) {
    result.remove(0, 1);
  }
  if (result.endsWith(QChar('"'))) {
    result.chop(1);
  }
  return result;
}

} // namespace

namespace GmicQt
{

LinkParameter::LinkParameter(QObject * parent) : AbstractParameter(parent), _alignment(Qt::AlignLeft) {}

LinkParameter::~LinkParameter()
{
  // Deleting a null (or already destroyed, thanks to QPointer) widget is harmless.
  delete _label.data();
}

int LinkParameter::size() const
{
  return 0;
}

bool LinkParameter::addTo(QWidget * widget, int row)
{
  _grid = widget ? dynamic_cast<QGridLayout *>(widget->layout()) : nullptr;
  Q_ASSERT_X(_grid, Q_FUNC_INFO, "No grid layout in widget");
  if (!_grid) {
    return false;
  }
  _row = row;
  delete _label.data();
  // Both the text and the URL are escaped: they are plain strings which must not be interpreted as markup
  // (a text such as "&lt;img src=...&gt;" is decoded to "<img src=...>" by HtmlTranslator::html2txt()).
  QString html;
  const bool allowed = isAllowedLinkUrl(parsedUrl(_url));
  if (allowed) {
    html = QString("<a href=\"%1\">%2</a>").arg(_url.toHtmlEscaped(), _text.toHtmlEscaped());
  } else {
    Logger::warning(QString("Link displayed as plain text (unsupported or invalid URL): %1").arg(_url));
    html = _text.toHtmlEscaped();
  }
  QLabel * label = new QLabel(html, widget);
  _label = label;
  label->setAlignment(_alignment);
  label->setTextFormat(Qt::RichText);
  label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  if (allowed) {
    label->setToolTip(_url.toHtmlEscaped()); // The displayed text may differ from the actual destination
  }
  setTextSelectable(label);
  connect(label, &QLabel::linkActivated, this, &LinkParameter::onLinkActivated);
  _grid->addWidget(label, row, 0, 1, 3);
  return true;
}

QString LinkParameter::value() const
{
  return QString();
}

QString LinkParameter::defaultValue() const
{
  return QString();
}

void LinkParameter::setValue(const QString &) {}

void LinkParameter::reset() {}

bool LinkParameter::initFromText(const QString & filterName, const char * text, int & textLength)
{
  const QList<QString> list = parseText("link", text, textLength);
  if (list.size() < 2) {
    return false;
  }
  // Syntax: link(url), link("text","url") or link(alignment,"text","url").
  // Commas inside quotes belong to the text or to the URL.
  QList<QString> values = splitArguments(list[1]);
  if (values.size() > 3) {
    Logger::error(QString("Invalid link parameter in filter [%1]: too many arguments (%2)").arg(filterName).arg(values.size()));
    return false;
  }

  _alignment = Qt::AlignCenter;
  if (values.size() == 3) {
    bool ok = false;
    const float a = values[0].toFloat(&ok);
    if (!ok) {
      return false;
    }
    if (a == 0.0f) {
      _alignment = Qt::AlignLeft;
    } else if (a == 1.0f) {
      _alignment = Qt::AlignRight;
    }
    values.pop_front();
  }

  _text.clear();
  if (values.size() == 2) {
    _text = unquoted(values[0]);
    _text = HtmlTranslator::html2txt(FilterTextTranslator::translate(_text, filterName));
    values.pop_front();
  }
  _url = unquoted(values.front()); // One value left (the list always has at least one element)
  if (_url.isEmpty()) {
    Logger::error(QString("Invalid link parameter in filter [%1]: empty URL").arg(filterName));
    return false;
  }
  if (_text.isEmpty()) {
    _text = _url;
  }
  return true;
}

void LinkParameter::onLinkActivated(const QString & link)
{
  const QUrl url = parsedUrl(link);
  if (!isAllowedLinkUrl(url)) {
    Logger::warning(QString("Refusing to open a link with an unsupported or invalid URL: %1").arg(link));
    return;
  }
  if (!QDesktopServices::openUrl(url)) {
    Logger::warning(QString("Could not open URL: %1").arg(link));
  }
}

} // namespace GmicQt



