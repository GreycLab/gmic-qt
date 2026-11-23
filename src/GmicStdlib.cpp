/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file GmicStdlib.cpp
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
#include "GmicStdlib.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <limits>
#include "Common.h"
#include "GmicQt.h"
#include "Logger.h"
#include "Utils.h"
#include "gmic.h"

namespace GmicQt
{

QByteArray GmicStdLib::Array;

namespace
{
// The G'MIC standard library is a few megabytes long: refuse to load a much larger "update" file.
const qint64 MaxStdlibFileSize = 128 * 1024 * 1024;
} // namespace

void GmicStdLib::loadStdLib() // TODO : Remove
{
  // Without configuration folder, the path would be relative: a file of the current folder would be read.
  const QString folder = gmicConfigPath(false);
  if (!folder.isEmpty()) {
    const QString path = QString("%1update%2.gmic").arg(folder).arg(gmic_version);
    const qint64 fileSize = QFileInfo(path).size();
    QFile stdlib(path);
    if ((fileSize > 0) && (fileSize <= MaxStdlibFileSize) && stdlib.open(QFile::ReadOnly)) {
      Array = stdlib.readAll();
      if (!Array.isEmpty()) { // Otherwise (read error), fall back to the built-in library
        return;
      }
    }
  }
  gmic_image<char> stdlib_h = gmic::decompress_stdlib();
  // Nothing (or only the terminating null character): Array[Array.size() - 1] would be invalid.
  // Too large: the size could not be stored in a QByteArray.
  if ((stdlib_h.size() < 2) || (stdlib_h.size() >= static_cast<unsigned long long>(std::numeric_limits<int>::max()))) {
    Logger::error("Could not decompress gmic builtin stdlib");
    Array.clear();
    return;
  }
  // Deep copy: fromRawData() would reference a buffer which is released when stdlib_h goes out of scope
  Array = QByteArray(stdlib_h.data(), static_cast<int>(stdlib_h.size()));
  Array[Array.size() - 1] = '\n';
}

QString GmicStdLib::substituteSourceVariables(QString text)
{
  // Built once (regular expressions are costly to build)
#ifdef _IS_WINDOWS_
  static const QRegularExpression reVariables("%([A-Za-z_][A-Za-z0-9_]+)%");
#else
  // $NAME or ${NAME}
  static const QRegularExpression reVariables("\\$(?:([A-Za-z_][A-Za-z0-9_]+)|\\{([A-Za-z_][A-Za-z0-9_]+)\\})");
#endif

  // The text is scanned only once: what is substituted is never scanned again. Previously, a variable whose
  // value contained a reference to itself (e.g. FOO='${FOO}'), or two variables referring to each other,
  // made this function loop forever while the text kept growing.
  QString result;
  int copiedUpTo = 0;
  QRegularExpressionMatchIterator it = reVariables.globalMatch(text);
  while (it.hasNext()) {
    const QRegularExpressionMatch match = it.next();
    QString name = match.captured(1);
    if (name.isEmpty()) {
      name = match.captured(2); // ${NAME} form
    }
    QString replacement;
    if (name == QLatin1String("VERSION")) {
      replacement = QString::number(GmicQt::GmicVersion);
    } else {
      const QByteArray value = qgetenv(name.toLocal8Bit().constData());
      if (value.isEmpty()) { // Undefined variables should yield an ignored source
        return {};
      }
      replacement = QString::fromLocal8Bit(value);
    }
    const int begin = int(match.capturedStart());
    result.append(text.mid(copiedUpTo, begin - copiedUpTo));
    result.append(replacement);
    copiedUpTo = int(match.capturedEnd());
  }
  if (copiedUpTo == 0) {
    return text; // No variable
  }
  result.append(text.mid(copiedUpTo));
  return result;
}

QStringList GmicStdLib::substituteSourceVariables(const QStringList & list)
{
  QStringList result;
  for (const QString & str : list) {
    QString source = substituteSourceVariables(str);
    if (!source.isEmpty()) {
      result << source;
    }
  }
  return result;
}

QByteArray GmicStdLib::hash()
{
  return QCryptographicHash::hash(Array, QCryptographicHash::Sha1);
}

} // namespace GmicQt






