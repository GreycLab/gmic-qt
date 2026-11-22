/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file ParametersCache.cpp
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
#include "ParametersCache.h"
#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include "Common.h"
#include "Globals.h"
#include "Logger.h"
#include "Utils.h"
#include "gmic.h"

namespace GmicQt
{

namespace
{

// The parameters file is normally a few kilobytes to a few megabytes long.
const qint64 MaxParametersFileSize = 64ll << 20;
const quint32 MaxUncompressedSize = 256u << 20;

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
    Logger::error(QString("Parameters file: announced uncompressed size is too large (%1 bytes)").arg(announcedSize));
    return QByteArray();
  }
  return qUncompress(data);
}

template <typename T> void removeObsoleteKeys(QHash<QString, T> & hash, const QSet<QString> & hashesToKeep)
{
  for (auto it = hash.begin(); it != hash.end();) {
    if (hashesToKeep.contains(it.key())) {
      ++it;
    } else {
      it = hash.erase(it);
    }
  }
}

} // namespace

QHash<QString, QList<QString>> ParametersCache::_parametersCache;
QHash<QString, InputOutputState> ParametersCache::_inOutPanelStates;
QHash<QString, QList<int>> ParametersCache::_visibilityStates;

void ParametersCache::load(bool loadFiltersParameters)
{
  // Load JSON file
  _parametersCache.clear();
  _inOutPanelStates.clear();
  _visibilityStates.clear();

  const QString jsonFilename = QString("%1%2").arg(gmicConfigPath(true), PARAMETERS_CACHE_FILENAME);
  QFile jsonFile(jsonFilename);
  if (!jsonFile.exists()) {
    return;
  }
  if (!jsonFile.open(QFile::ReadOnly)) {
    Logger::error("Cannot open " + jsonFilename);
    Logger::error("Parameters cannot be restored");
    return;
  }
  if (jsonFile.size() > MaxParametersFileSize) {
    Logger::error("Parameters file is too large: " + jsonFilename);
    Logger::warning("Last filters parameters are lost!");
    return;
  }
  const QByteArray allFile = jsonFile.readAll();
  QByteArray json;
  if (allFile.startsWith("{")) { // Was created in debug mode
    json = allFile;
  } else {
    json = safelyUncompress(allFile);
  }
  QJsonParseError parseError;
  const QJsonDocument jsonDoc = QJsonDocument::fromJson(json, &parseError);
  if (jsonDoc.isNull()) {
    Logger::warning(QString("Cannot parse ") + jsonFilename + " (" + parseError.errorString() + ")");
    Logger::warning("Last filters parameters are lost!");
    return;
  }
  if (!jsonDoc.isObject()) {
    Logger::error(QString("JSON file format is not correct (") + jsonFilename + ")");
    return;
  }

  const QJsonObject documentObject = jsonDoc.object();
  for (auto itFilter = documentObject.constBegin(); itFilter != documentObject.constEnd(); ++itFilter) {
    const QString hash = itFilter.key();
    const QJsonObject filterObject = itFilter.value().toObject();
    // Retrieve parameters
    if (loadFiltersParameters) {
      const QJsonValue parameters = filterObject.value("parameters");
      if (parameters.isArray()) {
        QStringList values;
        const QJsonArray array = parameters.toArray();
        for (const QJsonValue & v : array) {
          values.push_back(v.toString());
        }
        _parametersCache[hash] = values;
      }
      const QJsonValue visibilityStates = filterObject.value("visibility_states");
      if (visibilityStates.isArray()) {
        QList<int> values;
        const QJsonArray array = visibilityStates.toArray();
        for (const QJsonValue & v : array) {
          values.push_back(v.toInt());
        }
        _visibilityStates[hash] = values;
      }
    }
    // Retrieve Input/Output state
    const QJsonValue state = filterObject.value("in_out_state");
    if (state.isObject()) {
      _inOutPanelStates[hash] = InputOutputState::fromJSONObject(state.toObject());
    }
  }
}

void ParametersCache::save()
{
  // JSON Document format
  //
  // {
  //  "51d288e6f1c6e531cc61289f17e34d8a": {
  //      "parameters": [
  //          "6",
  //          "21.06",
  //          "1.36",
  //          "5",
  //          "0"
  //      ],
  //      "in_out_state": {
  //          "InputLayers": 1,
  //          "OutputMessages": 5,
  //          "OutputMode": 100,
  //          "PreviewMode": 100
  //      }
  //      "visibility_states": [
  //             0,
  //             1,
  //             2,
  //             0
  //      ]
  //  }
  // }

  QJsonObject documentObject;

  // Add Input/Output states
  for (auto itState = _inOutPanelStates.cbegin(); itState != _inOutPanelStates.cend(); ++itState) {
    QJsonObject filterObject;
    QJsonObject jsonState;
    itState.value().toJSONObject(jsonState);
    filterObject.insert("in_out_state", jsonState);
    documentObject.insert(itState.key(), filterObject);
  }

  // Add filters parameters
  for (auto itParams = _parametersCache.cbegin(); itParams != _parametersCache.cend(); ++itParams) {
    QJsonObject filterObject = documentObject.value(itParams.key()).toObject();
    // Add the parameters list
    QJsonArray array;
    for (const QString & str : itParams.value()) {
      array.push_back(str);
    }
    filterObject.insert("parameters", array);
    documentObject.insert(itParams.key(), filterObject);
  }

  // Add visibility states
  for (auto itVisibilities = _visibilityStates.cbegin(); itVisibilities != _visibilityStates.cend(); ++itVisibilities) {
    QJsonObject filterObject = documentObject.value(itVisibilities.key()).toObject();
    // Add the visibility states list
    QJsonArray array;
    for (const int state : itVisibilities.value()) {
      array.push_back(state);
    }
    filterObject.insert("visibility_states", array);
    documentObject.insert(itVisibilities.key(), filterObject);
  }

  const QJsonDocument jsonDoc(documentObject);
  const QString jsonFilename = QString("%1%2").arg(gmicConfigPath(true), PARAMETERS_CACHE_FILENAME);
#ifdef _GMIC_QT_DEBUG_
  const QByteArray array(jsonDoc.toJson());
#else
  const QByteArray array(qCompress(jsonDoc.toJson(QJsonDocument::Compact)));
#endif
  if (safelyWrite(array, jsonFilename)) {
    // Remove obsolete 2.0.0 pre-release files
    const QString path = gmicConfigPath(true);
    QFile::remove(path + "gmic_qt_parameters.dat");
    QFile::remove(path + "gmic_qt_parameters.json");
    QFile::remove(path + "gmic_qt_parameters.json.bak");
    QFile::remove(path + "gmic_qt_parameters_json.dat");
  } else {
    Logger::error("Cannot write " + jsonFilename);
    Logger::error("Parameters cannot be saved");
  }
}

void ParametersCache::setValues(const QString & hash, const QList<QString> & values)
{
  _parametersCache[hash] = values;
}

QList<QString> ParametersCache::getValues(const QString & hash)
{
  return _parametersCache.value(hash);
}

void ParametersCache::setVisibilityStates(const QString & hash, const QList<int> & states)
{
  _visibilityStates[hash] = states;
}

QList<int> ParametersCache::getVisibilityStates(const QString & hash)
{
  return _visibilityStates.value(hash);
}

void ParametersCache::remove(const QString & hash)
{
  _parametersCache.remove(hash);
  _inOutPanelStates.remove(hash);
  _visibilityStates.remove(hash);
}

InputOutputState ParametersCache::getInputOutputState(const QString & hash)
{
  return _inOutPanelStates.value(hash, InputOutputState(InputMode::Unspecified, DefaultOutputMode));
}

void ParametersCache::setInputOutputState(const QString & hash, const InputOutputState & state, const InputMode defaultInputMode)
{
  if ((state == InputOutputState(defaultInputMode, DefaultOutputMode)) //
      || (state == InputOutputState(InputMode::Unspecified, DefaultOutputMode))) {
    _inOutPanelStates.remove(hash);
    return;
  }
  _inOutPanelStates[hash] = state;
}

void ParametersCache::cleanup(const QSet<QString> & hashesToKeep)
{
  removeObsoleteKeys(_parametersCache, hashesToKeep);
  removeObsoleteKeys(_inOutPanelStates, hashesToKeep);
  removeObsoleteKeys(_visibilityStates, hashesToKeep);
}

} // namespace GmicQt
