/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file Misc.cpp
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

#include "Misc.h"
#include <QByteArray>
#include <QChar>
#include <QDebug>
#include <QMap>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <algorithm>
#include <cstring>
#include <vector>
#include "Common.h"
#include "Globals.h"
#include "HtmlTranslator.h"
#include "Logger.h"
#include "gmic.h"

namespace
{

// Locale-independent, ASCII-only character classification.
// The <cctype> functions have an undefined behavior when given a negative value other
// than EOF, which is what happens with plain (signed) chars holding UTF-8 bytes >= 0x80.
inline bool isAsciiSpace(char c)
{
  return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\v') || (c == '\f') || (c == '\r');
}

inline bool isAsciiAlnum(char c)
{
  return ((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z'));
}

inline void skipSpaces(const char *& pc)
{
  while (isAsciiSpace(*pc)) {
    ++pc;
  }
}

inline bool isEmptyOrSpaceSequence(const char * pc)
{
  while (*pc) {
    if (!isAsciiSpace(*pc)) {
      return false;
    }
    ++pc;
  }
  return true;
}
} // namespace

namespace GmicQt
{

QString commandFromOutputMessageMode(OutputMessageMode mode)
{
  switch (mode) {
  case OutputMessageMode::Quiet:
  case OutputMessageMode::VerboseConsole:
  case OutputMessageMode::VerboseLogFile:
  case OutputMessageMode::Unspecified:
    return "";
  case OutputMessageMode::VeryVerboseConsole:
  case OutputMessageMode::VeryVerboseLogFile:
    return "v 3";
  case OutputMessageMode::DebugConsole:
  case OutputMessageMode::DebugLogFile:
    return "debug";
  }
  return "";
}

void appendWithSpace(QString & str, const QString & other)
{
  if (str.isEmpty() || other.isEmpty()) {
    str += other;
    return;
  }
  str += QChar(' ');
  str += other;
}

void downcaseCommandTitle(QString & title)
{
  if (title.isEmpty()) {
    return;
  }
  // Regular expressions are compiled only once (they were rebuilt at each call).
  static const QRegularExpression acronymRegExp("([A-Z0-9]{2,255})");
  static const QRegularExpression dimensionRegExp("([1-9])[dD] ");
  static const QRegularExpression specialWordsRegExp("(B&amp;W|[ \\[]Lab|[ \\[]YCbCr)");
  static const QRegularExpression trailingCapitalRegExp(" ([A-Z])$");

  QMap<int, QString> acronyms;
  // Acronyms
  QRegularExpressionMatch match = acronymRegExp.match(title);
  while (match.hasMatch()) {
    const QString pattern = match.captured(0);
    acronyms[match.capturedStart(0)] = pattern;
    match = acronymRegExp.match(title, match.capturedStart(0) + pattern.length());
  }

  // 3D
  match = dimensionRegExp.match(title);
  if (match.hasMatch()) {
    acronyms[match.capturedStart(0)] = match.captured(1) + "d ";
  }

  // B&amp;W [Lab [YCbCr
  match = specialWordsRegExp.match(title);
  while (match.hasMatch()) {
    acronyms[match.capturedStart(1)] = match.captured(1);
    match = specialWordsRegExp.match(title, match.capturedStart(1) + match.capturedLength(1));
  }

  // Uppercase letter in last position, after a space
  match = trailingCapitalRegExp.match(title);
  if (match.hasMatch()) {
    acronyms[match.capturedStart()] = match.captured(0);
  }
  title = title.toLower();
  QMap<int, QString>::const_iterator it = acronyms.cbegin();
  while (it != acronyms.cend()) {
    // Lower-casing may (very rarely) change the length of a string: never write past its end.
    if ((it.key() >= 0) && (it.key() + it.value().length() <= title.length())) {
      title.replace(it.key(), it.value().length(), it.value());
    }
    ++it;
  }
  if (!title.isEmpty()) {
    title[0] = title[0].toUpper();
  }
}

bool parseGmicFilterParameters(const QString & text, QStringList & args)
{
  return parseGmicFilterParameters(text.toUtf8().constData(), args);
}

bool parseGmicFilterParameters(const char * text, QStringList & args)
{
  args.clear();
  if (!text) {
    return false;
  }
  skipSpaces(text);
  const char * pc = text;
  bool quoted = false;
  bool escaped = false;
  bool meaningfulSpaceFound = false;
  // RAII buffer (a raw new[] leaked if QString/QStringList allocation threw);
  // +1 : room for the terminating '\0'
  std::vector<char> buffer(std::strlen(pc) + 1, '\0');
  char * output = buffer.data();
  while (*pc && !((meaningfulSpaceFound = (!quoted && !escaped && isAsciiSpace(*pc))))) {
    if (escaped) {
      *output++ = *pc++;
      escaped = false;
    } else if (*pc == '\\') {
      escaped = true;
      ++pc;
    } else if (*pc == '"') {
      quoted = !quoted;
      ++pc;
    } else if (!quoted && !escaped && (*pc == ',')) {
      *output = '\0';
      args.push_back(QString::fromUtf8(buffer.data()));
      output = buffer.data();
      ++pc;
    } else {
      *output++ = *pc++;
    }
  }
  const bool endsWithComma = (output == buffer.data()) && (pc > text) && (!quoted && !escaped && (pc[-1] == ','));
  if ((output != buffer.data()) || endsWithComma) {
    *output = '\0';
    args.push_back(QString::fromUtf8(buffer.data()));
  }
  if (quoted || (meaningfulSpaceFound && !isEmptyOrSpaceSequence(pc))) {
    args.clear();
    return false;
  }
  return true;
}

bool parseGmicUniqueFilterCommand(const char * text, QString & command, QString & arguments)
{
  arguments.clear();
  command.clear();
  if (!text) {
    return false;
  }
  const char * commandBegin = text;
  skipSpaces(commandBegin);
  if (*commandBegin == '\0') {
    return false;
  }
  const char * pc = commandBegin;
  while (isAsciiAlnum(*pc) || (*pc == '_')) {
    ++pc;
  }
  if ((*pc != '\0') && !isAsciiSpace(*pc)) {
    return false;
  }
  const char * const commandEnd = pc;
  skipSpaces(pc);

  bool quoted = false;
  bool escaped = false;
  bool meaningfulSpaceFound = false;
  const char * argumentStart = pc;
  while (*pc && !((meaningfulSpaceFound = (!quoted && !escaped && isAsciiSpace(*pc))))) {
    if (escaped) {
      escaped = false;
    } else if (*pc == '\\') {
      escaped = true;
    } else if (*pc == '"') {
      quoted = !quoted;
    }
    ++pc;
  }
  if (quoted || (meaningfulSpaceFound && !isEmptyOrSpaceSequence(pc))) {
    return false;
  }
  command = QString::fromUtf8(commandBegin, static_cast<int>(commandEnd - commandBegin));
  arguments = QString::fromUtf8(argumentStart, static_cast<int>(pc - argumentStart));
  return true;
}

QString escapeUnescapedQuotes(const QString & text)
{
  // Works directly on the UTF-16 string: no round trip through std::string / UTF-8,
  // and no truncation at an embedded null character.
  QString result;
  result.reserve(text.size() + 8);
  bool escaped = false;
  for (const QChar c : text) {
    if (escaped) {
      escaped = false;
    } else if (c == QChar('\\')) {
      escaped = true;
    } else if (c == QChar('"')) {
      result += QChar('\\');
    }
    result += c;
  }
  if (escaped) {
    result += QChar('\\');
  }
  return result;
}

QString filterFullPathWithoutTags(const QList<QString> & path, const QString & name)
{
  QStringList noTags = {QString()};
  noTags.reserve(path.size() + 2);
  for (QString str : path) {
    if (str.startsWith(WarningPrefix)) {
      str.remove(0, 1);
    }
    noTags.push_back(HtmlTranslator::removeTags(str));
  }
  noTags.push_back(HtmlTranslator::removeTags(name));
  return noTags.join('/');
}

QString filterFullPathBasename(const QString & path)
{
  // Same result as removing "^.*/", without building a regular expression at each call.
  const int index = path.lastIndexOf(QChar('/'));
  return (index == -1) ? path : path.mid(index + 1);
}

QString flattenGmicParameterList(const QList<QString> & list, const QVector<bool> & quotedParameters)
{
  QString result;
  if (list.isEmpty()) {
    return result;
  }
  if (quotedParameters.size() < list.size()) {
    // The original code read past the end of quotedParameters in this case.
    Logger::warning(QString("flattenGmicParameterList(): %1 quoting flags for %2 parameters").arg(quotedParameters.size()).arg(list.size()));
  }
  for (int i = 0; i < list.size(); ++i) {
    if (i) {
      result += QChar(',');
    }
    const bool quote = (i < quotedParameters.size()) && quotedParameters[i];
    result += quote ? quotedString(list[i]) : list[i];
  }
  return result;
}

QStringList resolveUnchangedStatusValues(const QStringList & status, const QStringList & currentValues)
{
  QStringList result = status;
  for (int i = 0; i < result.size(); ++i) {
    if (result[i].isNull()) {
      result[i] = (i < currentValues.size()) ? currentValues[i] : QString("");
    }
  }
  return result;
}

QString mergedWithSpace(const QString & prefix, const QString & suffix)
{
  if (prefix.isEmpty() || suffix.isEmpty()) {
    return prefix + suffix;
  }
  return prefix + QChar(' ') + suffix;
}

QString elided(const QString & text, int width)
{
  if (text.length() <= width) {
    return text;
  }
  return text.left(std::max(0, width - 3)) + "...";
}

QVector<bool> quotedParameters(const QList<QString> & parameters)
{
  QVector<bool> result;
  result.reserve(parameters.size());
  for (const auto & str : parameters) {
    result.push_back(str.startsWith(QChar('"')));
  }
  return result;
}

QStringList mergeSubsequences(const QStringList & sequence, const QVector<int> & subSequenceLengths)
{
  QStringList result;
  QVector<int> lengths = subSequenceLengths;
  QStringList::const_iterator itInput = sequence.constBegin();
  QVector<int>::iterator itLength = lengths.begin();
  while ((itInput != sequence.constEnd()) && (itLength != lengths.end())) {
    if (*itLength <= 0) {
      ++itLength;
      continue;
    }
    QString text = *itInput++;
    --(*itLength);
    while (*itLength > 0) {
      if (itInput == sequence.constEnd()) {
        Logger::warning(QObject::tr("List %1 cannot be merged considering these runs: %2").arg(stringify(sequence)).arg(stringify(subSequenceLengths)));
        return QStringList();
      }
      text += QString(",%1").arg(*itInput++);
      --(*itLength);
    }
    result.push_back(text);
    ++itLength;
  }
  if ((itInput != sequence.constEnd()) || (itLength != lengths.end())) {
    Logger::warning(QObject::tr("List %1 cannot be merged considering these runs: %2").arg(stringify(sequence)).arg(stringify(subSequenceLengths)));
    return QStringList();
  }
  return result;
}

QStringList completePrefixFromFullList(const QStringList & prefix, const QStringList & fullList)
{
  if (fullList.size() <= prefix.size()) {
    return prefix;
  }
  QStringList result = prefix;
  result += fullList.mid(prefix.size());
  return result;
}

QString quotedString(QString text)
{
  return QString("\"%1\"").arg(escapeUnescapedQuotes(text));
}

QStringList quotedStringList(const QStringList & stringList)
{
  QStringList result;
  result.reserve(stringList.size());
  for (const auto & text : stringList) {
    result.push_back(quotedString(text));
  }
  return result;
}

QString unescaped(const QString & text)
{
  QByteArray ba = text.toUtf8();
  if (!ba.isEmpty()) {
    gmic_library::cimg::strunescape(ba.data());
  }
  return QString::fromUtf8(ba.constData());
}

QString unquoted(const QString & text)
{
  static const QRegularExpression re("^\\s*\"(.*)\"\\s*$");
  auto match = re.match(text);
  if (match.hasMatch()) {
    return match.captured(1);
  } else {
    return text.trimmed();
  }
}

QStringList expandParameterList(const QStringList & parameters, const QVector<int> & sizes)
{
  QStringList result;
  if (parameters.size() != sizes.size()) {
    // Previously only checked by Q_ASSERT_X (i.e. silently ignored in release builds).
    Logger::warning(QString("expandParameterList(): sizes are different (parameters: %1, sizes: %2)").arg(parameters.size()).arg(sizes.size()));
  }
  Q_ASSERT_X(parameters.size() == sizes.size(), __PRETTY_FUNCTION__, QString("Sizes are different (parameters: %1, sizes: %2)").arg(parameters.size()).arg(sizes.size()).toStdString().c_str());
  QStringList::const_iterator itParam = parameters.constBegin();
  auto itSize = sizes.constBegin();
  while (itParam != parameters.constEnd() && itSize != sizes.constEnd()) {
    if (*itSize > 1) {
      result.append(itParam->split(","));
    } else if (*itSize == 1) {
      result.push_back(*itParam);
    } else {
      Logger::warning(QString("expandParameterList(): parameter size should be at least 1 (it is %1)").arg(*itSize));
      Q_ASSERT_X((*itSize >= 1), __PRETTY_FUNCTION__, QString("Parameter size should be at least 1 (it is %1)").arg(*itSize).toStdString().c_str());
    }
    ++itParam;
    ++itSize;
  }
  return result;
}

QString readableDuration(qint64 ms)
{
  const qint64 HOUR = 3600000;
  const qint64 MINUTE = 60000;
  const qint64 SECOND = 1000;
  if (ms < SECOND) {
    return QString("%1 ms").arg(ms);
  }
  if (ms < MINUTE) {
    return QString("%1 s %2 ms").arg(ms / SECOND).arg(ms % SECOND);
  }
  const int hours = static_cast<int>(ms / HOUR);
  return QString("%1:%2:%3.%4")                             //
      .arg(ms / HOUR, (hours < 10) ? 2 : 0, 10, QChar('0')) // Hours
      .arg((ms % HOUR) / MINUTE, 2, 10, QChar('0'))         // Minutes
      .arg((ms % MINUTE) / 1000, 2, 10, QChar('0'))         // Seconds
      .arg(ms % SECOND, 3, 10, QChar('0'));                 // Milliseconds
}

QString readableSize(quint64 n)
{
  // Explicit 64-bit constants (1ul is only 32 bits wide on Windows).
  constexpr quint64 KiB = Q_UINT64_C(1) << 10;
  constexpr quint64 MiB = Q_UINT64_C(1) << 20;
  constexpr quint64 GiB = Q_UINT64_C(1) << 30;
  if (n >= GiB) {
    return QString(QObject::tr("%1 GiB")).arg(n / static_cast<double>(GiB), 0, 'f', 1);
  } else if (n >= MiB) {
    return QString(QObject::tr("%1 MiB")).arg(n / static_cast<double>(MiB), 0, 'f', 1);
  } else if (n >= KiB) {
    return QString(QObject::tr("%1 KiB")).arg(n / static_cast<double>(KiB), 0, 'f', 1);
  } else {
    return QString(QObject::tr("%1 B")).arg(n);
  }
}

qreal randomReal(qreal lowest, qreal highest)
{
  // generateDouble() has a 53-bit resolution and is uniformly distributed in [0,1)
  // (the former implementation was limited to 32 bits of randomness).
  const qreal t = QRandomGenerator::global()->generateDouble();
  return (1 - t) * lowest + t * highest;
}

} // namespace GmicQt
