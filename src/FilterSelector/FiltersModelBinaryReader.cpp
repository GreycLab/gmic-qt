/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file FiltersModelBinaryReader.cpp
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
#include "FilterSelector/FiltersModelBinaryReader.h"
#include <QByteArray>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QMap>
#include <cmath>
#include "Common.h"
#include "FilterSelector/FiltersModel.h"
#include "Globals.h"
#include "Logger.h"

namespace GmicQt
{

namespace
{
const quint32 MagicNumber = 0x03300330;
const quint32 MaxSupportedVersion = 100;
// The cache is a few megabytes at most. A much larger file is either corrupted
// or not a cache at all, so it is not worth trying to load it.
const qint64 MaxCacheFileSize = 256ll << 20;
} // namespace

FiltersModelBinaryReader::FiltersModelBinaryReader(FiltersModel & model) : _model(model) {}

bool FiltersModelBinaryReader::read(const QString & filename)
{
  TIMING;
  QFile file(filename);
  if (!file.open(QFile::ReadOnly)) {
    return false;
  }
  if (file.size() > MaxCacheFileSize) {
    Logger::warning("Filters binary cache: file is too large");
    return false;
  }
  QDataStream stream(&file);
  QByteArray hash;

  if (!readHeader(stream, hash)) {
    return false;
  }

  // Filters are first read into a temporary map, so that a truncated or corrupted
  // cache (e.g. interrupted write, full disk) can never leave the model half filled.
  QMap<QString, FiltersModel::Filter> filters;
  while (!stream.atEnd()) {
    FiltersModel::Filter filter;
    if (!readFilter(stream, filter)) {
      Logger::warning("Filters binary cache: corrupted or truncated file");
      return false;
    }
    filters.insert(filter.hash(), filter);
  }
  if (stream.status() != QDataStream::Ok) {
    Logger::warning("Filters binary cache: read error");
    return false;
  }

  for (auto it = filters.cbegin(); it != filters.cend(); ++it) {
    _model._hash2filter.insert(it.key(), it.value());
  }
  TIMING;
  return true;
}

QByteArray FiltersModelBinaryReader::readHash(const QString & filename)
{
  QByteArray hash;
  QFile file(filename);
  if (!file.open(QFile::ReadOnly)) {
    return QByteArray();
  }
  QDataStream stream(&file);
  if (!readHeader(stream, hash)) {
    return QByteArray();
  }
  return hash;
}

bool FiltersModelBinaryReader::readHeader(QDataStream & stream, QByteArray & hash)
{
  quint32 magic = 0;
  stream >> magic;
  if ((stream.status() != QDataStream::Ok) || (magic != MagicNumber)) {
    Logger::warning("Filters binary cache: wrong magic number");
    return false;
  }
  quint32 version = 0;
  stream >> version;
  if (stream.status() != QDataStream::Ok) {
    Logger::warning("Filters binary cache: cannot read version");
    return false;
  }
  if (version > MaxSupportedVersion) {
    Logger::warning("Filters binary cache: unsupported version");
    return false;
  }
  stream.setVersion(QDataStream::Qt_5_0);

  stream >> hash;
  if ((stream.status() != QDataStream::Ok) || hash.isEmpty()) {
    Logger::warning("Filters binary cache: cannot read hash");
    hash.clear();
    return false;
  }
  return true;
}

bool FiltersModelBinaryReader::readString(QDataStream & stream, QString & string)
{
  QByteArray array;
  stream >> array;
  if (stream.status() != QDataStream::Ok) {
    return false;
  }
  string = QString::fromUtf8(array);
  return true;
}

bool FiltersModelBinaryReader::readStringList(QDataStream & stream, QList<QString> & list)
{
  list.clear();
  quint8 size = 0;
  stream >> size;
  if (stream.status() != QDataStream::Ok) {
    return false;
  }
  QString string;
  while (size--) {
    if (!readString(stream, string)) {
      list.clear();
      return false;
    }
    list.push_back(string);
  }
  return true;
}

bool FiltersModelBinaryReader::isValidInputMode(quint8 value)
{
  // Deprecated modes (removed since 2.8.2) are never written to the cache.
  return (value <= static_cast<quint8>(InputMode::AllInvisible)) || (value == static_cast<quint8>(InputMode::Unspecified));
}

bool FiltersModelBinaryReader::readFilter(QDataStream & stream, FiltersModel::Filter & filter)
{
  if (!(readString(stream, filter._name)                      //
        && readString(stream, filter._plainText)              //
        && readString(stream, filter._translatedPlainText)    //
        && readStringList(stream, filter._path)               //
        && readStringList(stream, filter._plainPath)          //
        && readStringList(stream, filter._translatedPlainPath) //
        && readString(stream, filter._command)                //
        && readString(stream, filter._previewCommand))) {
    return false;
  }

  quint8 inputMode = 0;
  stream >> inputMode;
  if ((stream.status() != QDataStream::Ok) || !isValidInputMode(inputMode)) {
    return false;
  }
  filter._defaultInputMode = InputMode(inputMode);

  if (!readString(stream, filter._parameters)) {
    return false;
  }

  float previewFactor = PreviewFactorAny;
  bool isAccurateIfZoomed = false;
  bool previewFromFullImage = false;
  stream >> previewFactor >> isAccurateIfZoomed >> previewFromFullImage;
  if ((stream.status() != QDataStream::Ok) || !std::isfinite(previewFactor)) {
    return false;
  }
  filter._previewFactor = previewFactor;
  filter._isAccurateIfZoomed = isAccurateIfZoomed;
  filter._previewFromFullImage = previewFromFullImage;

  if (!readString(stream, filter._hash) || filter._hash.isEmpty()) {
    return false;
  }
  bool isWarning = false;
  stream >> isWarning;
  if (stream.status() != QDataStream::Ok) {
    return false;
  }
  filter._isWarning = isWarning;
  return true;
}

} // namespace GmicQt
