/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file FilterTagMap.h
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
#include "FilterTagMap.h"
#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <algorithm>
#include <limits>
#include "Common.h"
#include "Globals.h"
#include "GmicQt.h"
#include "Logger.h"
#include "Utils.h"

namespace GmicQt
{

namespace
{

// A tags file contains a few bytes per tagged filter. These (very generous) limits only protect
// us from a corrupted file, or from a hostile one: qUncompress() trusts the uncompressed size
// stored in the first four bytes of its input, and allocates that much memory before decompressing.
const qint64 MaxTagsFileSize = 16 * 1024 * 1024;
const quint32 MaxTagsUncompressedSize = 64 * 1024 * 1024;

// Size announced in the header of data compressed with qCompress() (big endian, 4 bytes).
bool declaredUncompressedSize(const QByteArray & data, quint32 & size)
{
  if (data.size() < 4) {
    return false;
  }
  const uchar * bytes = reinterpret_cast<const uchar *>(data.constData());
  size = (static_cast<quint32>(bytes[0]) << 24) | (static_cast<quint32>(bytes[1]) << 16) | (static_cast<quint32>(bytes[2]) << 8) | static_cast<quint32>(bytes[3]);
  return true;
}

} // namespace

QMap<QString, TagColorSet> FiltersTagMap::_hashesToColors;

TagColorSet FiltersTagMap::filterTags(const QString & hash)
{
  auto it = _hashesToColors.find(hash);
  if (it == _hashesToColors.end()) {
    return TagColorSet::Empty;
  }
  return it.value();
}

void FiltersTagMap::setFilterTags(const QString & hash, const TagColorSet & colors)
{
  if (colors.isEmpty()) {
    _hashesToColors.remove(hash);
    return;
  }
  _hashesToColors[hash] = colors;
}

void FiltersTagMap::load()
{
  _hashesToColors.clear();
  const QString configPath = gmicConfigPath(false);
  if (configPath.isEmpty()) { // No configuration folder (yet): nothing to restore. Do not look in the current folder.
    return;
  }
  const QString jsonFilename = configPath + FILTERS_TAGS_FILENAME;
  QFile jsonFile(jsonFilename);
  if (!jsonFile.exists()) {
    return;
  }
  if (!jsonFile.open(QFile::ReadOnly)) {
    Logger::error("Cannot open " + jsonFilename);
    Logger::error("Tags cannot be restored");
    return;
  }
  if (jsonFile.size() > MaxTagsFileSize) {
    Logger::warning(QString("File is too large to be a tags file (") + jsonFilename + ")");
    Logger::warning("Filter tags are lost!");
    return;
  }
  const QByteArray allFile = jsonFile.read(MaxTagsFileSize);
  QByteArray jsonData;
  if (allFile.startsWith("{")) { // Was created in debug mode
    jsonData = allFile;
  } else {
    quint32 uncompressedSize = 0;
    if (!declaredUncompressedSize(allFile, uncompressedSize) || (uncompressedSize > MaxTagsUncompressedSize)) {
      Logger::warning(QString("Cannot parse ") + jsonFilename);
      Logger::warning("Filter tags are lost!");
      return;
    }
    jsonData = qUncompress(allFile);
  }
  const QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData);
  if (jsonDoc.isNull()) {
    Logger::warning(QString("Cannot parse ") + jsonFilename);
    Logger::warning("Filter tags are lost!");
    return;
  }
  if (!jsonDoc.isObject()) {
    Logger::error(QString("JSON file format is not correct (") + jsonFilename + ")");
    return;
  }
  const QJsonObject documentObject = jsonDoc.object();
  for (QJsonObject::const_iterator it = documentObject.constBegin(); //
       it != documentObject.constEnd();                              //
       ++it) {
    const QJsonValue value = it.value();
    if (!value.isDouble()) { // Ignore invalid entries (strings, objects...)
      continue;
    }
    const double mask = value.toDouble();
    if (!(mask >= 0.0 && mask <= static_cast<double>(std::numeric_limits<unsigned int>::max()))) {
      continue;
    }
    // The TagColorSet constructor drops the bits that do not correspond to any known color.
    const TagColorSet colors(static_cast<unsigned int>(mask));
    if (!colors.isEmpty()) {
      _hashesToColors[it.key()] = colors;
    }
  }
}

void FiltersTagMap::save()
{
  QJsonObject documentObject;
  for (auto it = _hashesToColors.cbegin(); it != _hashesToColors.cend(); ++it) {
    if (!it.value().isEmpty()) {
      documentObject.insert(it.key(), QJsonValue(static_cast<int>(it.value().mask())));
    }
  }
  const QJsonDocument jsonDoc(documentObject);
  const QString configPath = gmicConfigPath(true);
  if (configPath.isEmpty()) { // Do not write the file in the current folder
    Logger::error("Cannot find or create the G'MIC configuration folder");
    Logger::error("Tags cannot be saved");
    return;
  }
  const QString jsonFilename = configPath + FILTERS_TAGS_FILENAME;
  if (QFile::exists(jsonFilename)) {
    const QString bakFilename = jsonFilename + ".bak";
    QFile::remove(bakFilename);
    QFile::copy(jsonFilename, bakFilename);
  }

#ifdef _GMIC_QT_DEBUG_
  const bool ok = safelyWrite(jsonDoc.toJson(), jsonFilename);
#else
  const bool ok = safelyWrite(qCompress(jsonDoc.toJson(QJsonDocument::Compact)), jsonFilename);
#endif
  if (!ok) {
    Logger::error("Cannot write " + jsonFilename);
    Logger::error("Tags cannot be saved");
  }
}

TagColorSet FiltersTagMap::usedColors(int * count)
{
  if (count) {
    std::fill_n(count, static_cast<int>(TagColor::Count), 0);
  }
  TagColorSet all;
  for (auto it = _hashesToColors.cbegin(); it != _hashesToColors.cend(); ++it) {
    const TagColorSet colors = it.value();
    all |= colors;
    if (count) {
      for (TagColor color : colors) { // Only valid colors (the mask is truncated by TagColorSet)
        ++count[static_cast<int>(color)];
      }
    }
  }
  return all;
}

void FiltersTagMap::removeAllTags(TagColor color)
{
  if (!isValidTagColor(color)) {
    return;
  }
  auto it = _hashesToColors.begin();
  while (it != _hashesToColors.end()) {
    it.value() -= color;
    if (it.value().isEmpty()) {
      it = _hashesToColors.erase(it);
    } else {
      ++it;
    }
  }
}

void FiltersTagMap::clearFilterTag(const QString & hash, TagColor color)
{
  if (!isValidTagColor(color)) {
    return;
  }
  auto it = _hashesToColors.find(hash);
  if (it == _hashesToColors.end()) {
    return;
  }
  it.value() -= color;
  if (it.value().isEmpty()) {
    _hashesToColors.erase(it);
  }
}

void FiltersTagMap::setFilterTag(const QString & hash, TagColor color)
{
  if (!isValidTagColor(color)) {
    return;
  }
  setFilterTags(hash, filterTags(hash) + color);
}

void FiltersTagMap::toggleFilterTag(const QString & hash, TagColor color)
{
  if (!isValidTagColor(color)) {
    return;
  }
  // Going through setFilterTags() removes the entry when the set becomes empty,
  // instead of keeping (and saving) an empty set.
  TagColorSet colors = filterTags(hash);
  colors.toggle(color);
  setFilterTags(hash, colors);
}

void FiltersTagMap::remove(const QString & hash)
{
  _hashesToColors.remove(hash);
}

} // namespace GmicQt
