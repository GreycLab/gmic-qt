/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file NoteParameter.cpp
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
#include "FilterParameters/NoteParameter.h"
#include <QDebug>
#include <QDesktopServices>
#include <QGridLayout>
#include <QLabel>
#include <QRegularExpression>
#include <QUrl>
#include "HtmlTranslator.h"
#include "Logger.h"
#include "Settings.h"

namespace
{

// The text of a note is HTML which comes from a filter definition, i.e. from a source that may be
// downloaded from the Internet or supplied by the user. It may contain links such as
// <a href="file:///..."> or a link which uses any scheme registered by an application, and these
// could launch arbitrary programs or documents. Only web and e-mail links are opened
// (same rule as for the link parameters, see LinkParameter.cpp).
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

} // namespace

namespace GmicQt
{

NoteParameter::NoteParameter(QObject * parent) : AbstractParameter(parent), _label(nullptr) {}

NoteParameter::~NoteParameter()
{
  delete _label;
}

int NoteParameter::size() const
{
  return 0;
}

bool NoteParameter::addTo(QWidget * widget, int row)
{
  _grid = widget ? dynamic_cast<QGridLayout *>(widget->layout()) : nullptr;
  Q_ASSERT_X(_grid, Q_FUNC_INFO, "No grid layout in widget");
  if (!_grid) { // Without this test, a release build would dereference a null pointer below
    return false;
  }
  _row = row;
  delete _label;
  _label = nullptr;
  _label = new QLabel(_text, widget);
  _label->setTextFormat(Qt::RichText);
  _label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  _label->setWordWrap(true);
  _label->setOpenExternalLinks(false); // Links are handled (and checked) by onLinkActivated()
  setTextSelectable(_label);
  connect(_label, &QLabel::linkActivated, this, &NoteParameter::onLinkActivated);
  _grid->addWidget(_label, row, 0, 1, 3);
  return true;
}

QString NoteParameter::value() const
{
  return QString();
}

QString NoteParameter::defaultValue() const
{
  return QString();
}

void NoteParameter::setValue(const QString &) {}

void NoteParameter::reset() {}

bool NoteParameter::initFromText(const QString & /* filterName */, const char * text, int & textLength)
{
  QList<QString> list = parseText("note", text, textLength);
  if (list.size() < 2) {
    return false;
  }

  // The regular expressions are built once (this function is called for each note of each filter that is loaded)
  static const QRegularExpression colorAttribute("color\\s*=\\s*\"");
  static const QRegularExpression foregroundAttribute("foreground\\s*=\\s*\"");
  static const QRegularExpression purpleColor("color\\s*=\\s*\"purple\"");
  static const QRegularExpression purpleForeground("foreground\\s*=\\s*\"purple\"");
  static const QRegularExpression blueColor("color\\s*=\\s*\"blue\"");
  static const QRegularExpression blueForeground("foreground\\s*=\\s*\"blue\"");

  _text = list[1].trimmed(); // Notes are never translated
  if (_text.startsWith(QChar('"'))) {
    _text.remove(0, 1);
  }
  if (_text.endsWith(QChar('"'))) {
    _text.chop(1);
  }
  _text.replace(QString("\\\""), "\"");
  _text.replace(QString("\\n"), "<br/>");

  if (Settings::darkThemeEnabled()) {
    _text.replace(purpleColor, QString("color=\"#ff00ff\""));
    _text.replace(purpleForeground, QString("foreground=\"#ff00ff\""));
    _text.replace(blueColor, QString("color=\"#9b9bff\""));
    _text.replace(blueForeground, QString("foreground=\"#9b9bff\""));
  }

  _text.replace(colorAttribute, QString("style=\"color:"));
  _text.replace(foregroundAttribute, QString("style=\"color:"));
  _text = HtmlTranslator::fromUtf8Escapes(_text);
  return true;
}

void NoteParameter::onLinkActivated(const QString & link)
{
  const QUrl url(link);
  if (!isAllowedLinkUrl(url)) {
    Logger::warning(QString("Link in a note ignored (unsupported or invalid URL): %1").arg(link));
    return;
  }
  QDesktopServices::openUrl(url);
}

} // namespace GmicQt
