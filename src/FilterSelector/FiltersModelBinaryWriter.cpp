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
#include "FilterSelector/FiltersModelBinaryWriter.h"
#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QList>
#include <QMap>
#include <QString>
#include <limits>
#include "Common.h"
#include "FilterSelector/FiltersModel.h"
#include "GmicQt.h"
#include "Logger.h"
#include "Utils.h"

namespace GmicQt
{

FiltersModelBinaryWriter::FiltersModelBinaryWriter(const FiltersModel & model) : _model(model) {}

bool FiltersModelBinaryWriter::write(const QString & filename, const QByteArray & hash)
{
  TIMING;
  if (hash.isEmpty()) {
    // The reader rejects a cache without hash: do not write a useless file.
    Logger::warning("Filters binary cache: not written (empty hash)");
    return false;
  }

  // The whole cache is first serialized in memory, then written at once and atomically.
  // Previously, a failure while writing (full disk, crash...) left a truncated cache
  // which was indistinguishable from a valid one when only its header was checked.
  QByteArray buffer;
  QDataStream stream(&buffer, QIODevice::WriteOnly);

  stream << (quint32)0x03300330;
  stream << (quint32)100;
  stream.setVersion(QDataStream::Qt_5_0);

  stream << hash;

  QMap<QString, FiltersModel::Filter>::const_iterator it = _model._hash2filter.cbegin();
  const QMap<QString, FiltersModel::Filter>::const_iterator end = _model._hash2filter.cend();

  while (it != end) {
    const FiltersModel::Filter & filter = it.value();
    stream << filter._name.toUtf8();
    stream << filter._plainText.toUtf8();
    stream << filter._translatedPlainText.toUtf8();
    if (!writeStringList(filter._path, stream) || !writeStringList(filter._plainPath, stream) || !writeStringList(filter._translatedPlainPath, stream)) {
      Logger::error(QString("Filters binary cache: not written (path too long for filter %1)").arg(filter._plainText));
      return false;
    }
    stream << filter._command.toUtf8();
    stream << filter._previewCommand.toUtf8();
    stream << quint8(filter._defaultInputMode);
    stream << filter._parameters.toUtf8();
    stream << filter._previewFactor;
    stream << filter._isAccurateIfZoomed;
    stream << filter._previewFromFullImage;
    stream << filter._hash.toUtf8();
    stream << filter._isWarning;
    ++it;
  }

  if (stream.status() != QDataStream::Ok) {
    Logger::error("Filters binary cache: serialization error");
    return false;
  }
  const bool ok = safelyWrite(buffer, filename);
  TIMING;
  return ok;
}

bool FiltersModelBinaryWriter::writeStringList(const QList<QString> & list, QDataStream & stream)
{
  // The list size is stored on one byte: refuse to silently truncate it
  // (which would desynchronize the reader and corrupt the whole cache).
  if (list.size() > static_cast<int>(std::numeric_limits<quint8>::max())) {
    return false;
  }
  stream << static_cast<quint8>(list.size());
  for (const QString & str : list) {
    stream << str.toUtf8();
  }
  return true;
}

} // namespace GmicQt
