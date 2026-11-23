/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file HtmlTranslator.cpp
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

#include "HtmlTranslator.h"
#include <QDebug>
#include <QHash>
#include <QRegularExpression>
#include <mutex>
#include "Common.h"
#include "gmic.h"

namespace GmicQt
{

QTextDocument HtmlTranslator::_document;

namespace
{
// QTextDocument is neither thread-safe nor cheap: setHtml() is by far the most expensive operation
// of the filters tree (it was called for every item, each time the tree was rebuilt, i.e. at each
// keystroke in the search field). Results are therefore memoized, and plain strings skip it.
std::mutex & documentMutex()
{
  static std::mutex mutex;
  return mutex;
}

QHash<QString, QString> & memoizedResults()
{
  static QHash<QString, QString> results;
  return results;
}

const int MaxMemoizedResults = 50000;

/**
 * True if QTextDocument::setHtml() followed by toPlainText() would give back exactly the same text:
 * no markup or entity ('&' or '<'), no character that would be altered by the conversion (control
 * characters, line and paragraph separators, non-breaking spaces...) and no white space which would be
 * collapsed or trimmed (only isolated spaces between two other characters are kept as is).
 */
bool isUnalteredByHtmlConversion(const QString & str)
{
  const int size = str.size();
  QChar previous;
  for (int i = 0; i < size; ++i) {
    const QChar c = str.at(i);
    const ushort u = c.unicode();
    if ((u == '&') || (u == '<') || (u < 0x20) || (u == 0x7F) || (u == 0x2028) || (u == 0x2029) || (u == 0xFFFC)) {
      return false;
    }
    if (c.isSpace() && ((u != ' ') || (i == 0) || (i == size - 1) || (previous == QChar(' ')))) {
      return false;
    }
    previous = c;
  }
  return true;
}
} // namespace

QString HtmlTranslator::removeTags(QString str)
{
  // Building a regular expression is costly: it is done only once.
  static const QRegularExpression tags("<[^>]*>");
  return str.remove(tags);
}

// TODO : enum param force + enum param translate
QString HtmlTranslator::html2txt(const QString & str, bool force)
{
  if (force || hasHtmlEntities(str)) {
    if (isUnalteredByHtmlConversion(str)) {
      return fromUtf8Escapes(str);
    }
    std::lock_guard<std::mutex> lock(documentMutex());
    QHash<QString, QString> & memo = memoizedResults();
    const auto it = memo.constFind(str);
    if (it != memo.constEnd()) {
      return it.value();
    }
    _document.setHtml(str);
    const QString result = fromUtf8Escapes(_document.toPlainText());
    if (memo.size() >= MaxMemoizedResults) {
      memo.clear();
    }
    memo.insert(str, result);
    return result;
  }
  return fromUtf8Escapes(str);
}

bool HtmlTranslator::hasHtmlEntities(const QString & str)
{
  // Every entity or tag starts with '&' or '<': most of the strings are rejected right away.
  if (!str.contains(QChar('&')) && !str.contains(QChar('<'))) {
    return false;
  }
  // The three regular expressions were built (and scanned) at each call: a single one, built once.
  static const QRegularExpression entitiesOrTags("&[a-zA-Z]+;|&#x?[0-9A-Fa-f]+;|</?[a-zA-Z]*>|<[a-zA-Z]*/>");
  return str.contains(entitiesOrTags);
}

QString HtmlTranslator::fromUtf8Escapes(const QString & str)
{
  if (str.isEmpty()) {
    return str;
  }
  QByteArray ba = str.toUtf8();
  gmic_library::cimg::strunescape(ba.data());
  return QString::fromUtf8(ba);
}

} // namespace GmicQt





