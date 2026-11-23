/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file FiltersVisibilityMap.cpp
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
#include "FiltersVisibilityMap.h"
#include <QBuffer>
#include <QByteArray>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include "Common.h"
#include "Globals.h"
#include "GmicQt.h"
#include "Logger.h"
#include "Utils.h"

namespace GmicQt
{

namespace
{

// The visibility file is normally a few kilobytes long.
const qint64 MaxVisibilityFileSize = 16ll << 20;
const quint32 MaxUncompressedSize = 64u << 20;
const qint32 MaxHiddenFiltersCount = 1000000;

/*
 * qUncompress() trusts the size announced in the first four bytes (big endian) of the data and
 * allocates that much memory: refuse absurd values coming from a corrupted file.
 */
QByteArray safelyUncompress(const QByteArray & data)
{
  if (data.size() < 4) {
    return QByteArray();
  }
  const uchar * p = reinterpret_cast<const uchar *>(data.constData());
  const quint32 announcedSize = (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
  if (announcedSize > MaxUncompressedSize) {
    Logger::error(QString("Visibility file: announced uncompressed size is too large (%1 bytes)").arg(announcedSize));
    return QByteArray();
  }
  return qUncompress(data);
}

} // namespace

QSet<QString> FiltersVisibilityMap::_hiddenFilters;

bool FiltersVisibilityMap::filterIsVisible(const QString & hash)
{
  return !_hiddenFilters.contains(hash);
}

void FiltersVisibilityMap::setVisibility(const QString & hash, bool visible)
{
  if (visible) {
    _hiddenFilters.remove(hash);
  } else {
    _hiddenFilters.insert(hash);
  }
}

void FiltersVisibilityMap::load()
{
  QString path = QString("%1%2").arg(gmicConfigPath(false), FILTERS_VISIBILITY_FILENAME);
  QFile file(path);
  if (!file.open(QFile::ReadOnly)) {
    return; // No file (yet)
  }
  if (file.size() > MaxVisibilityFileSize) {
    Logger::error("Visibility file is too large (" + file.fileName() + ")");
    return;
  }
  // Skip the header lines, up to the marker of the compressed data
  const QByteArray marker("[Hidden filters list (compressed)]\n");
  bool markerFound = false;
  while (!file.atEnd()) {
    if (file.readLine(1024) == marker) {
      markerFound = true;
      break;
    }
  }
  if (!markerFound) {
    Logger::error("Cannot read visibility file (" + file.fileName() + ")");
    return;
  }
  QByteArray data = safelyUncompress(file.readAll());
  QBuffer buffer(&data);
  if (!buffer.open(QIODevice::ReadOnly)) {
    return;
  }

  bool ok = false;
  const qint32 count = buffer.readLine(1024).trimmed().toInt(&ok);
  // A negative count used to be turned into a loop of about 2^32 iterations by "while (count--)".
  if (!ok || (count < 0) || (count > MaxHiddenFiltersCount)) {
    Logger::error("Cannot read visibility file (" + file.fileName() + ")");
    return;
  }
  for (qint32 i = 0; (i < count) && !buffer.atEnd(); ++i) {
    const QString hash = QString::fromLatin1(buffer.readLine(1024).trimmed());
    if (!hash.isEmpty()) {
      _hiddenFilters.insert(hash);
    }
  }
}

void FiltersVisibilityMap::save()
{
  QByteArray data = QByteArray::number(static_cast<qint32>(_hiddenFilters.size())) + '\n';
  for (const QString & str : _hiddenFilters) {
    data += str.toLatin1();
    data += '\n';
  }
  QString path = QString("%1%2").arg(gmicConfigPath(true), FILTERS_VISIBILITY_FILENAME);
  QByteArray array = QString("Version=%1\n[Hidden filters list (compressed)]\n").arg(gmicVersionString()).toLocal8Bit();
  array += qCompress(data);
  if (!safelyWrite(array, path)) {
    Logger::error("Saving filters visibility in " + path);
  }
}

} // namespace GmicQt


