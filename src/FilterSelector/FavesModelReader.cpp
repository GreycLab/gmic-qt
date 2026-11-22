/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file FavesModelReader.cpp
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
#include "FilterSelector/FavesModelReader.h"
#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>
#include "FilterSelector/FavesModel.h"
#include "Logger.h"
#include "Utils.h"
#include "gmic.h"

namespace GmicQt
{

namespace
{

// A faves file is a few kilobytes long (a few megabytes at most). A much larger file
// is corrupted or is not a faves file, and is not worth being loaded in memory.
const qint64 MaxFavesFileSize = 64ll << 20;

bool readFileContents(QFile & file, QByteArray & contents)
{
  if (file.size() > MaxFavesFileSize) {
    Logger::error(QString("File is too large to be a faves file: %1").arg(file.fileName()));
    return false;
  }
  contents = file.readAll();
  if (file.error() != QFileDevice::NoError) {
    Logger::error(QString("Read error (%1): %2").arg(file.fileName(), file.errorString()));
    return false;
  }
  return true;
}

QList<QString> splitLines(const QByteArray & contents)
{
  QList<QString> lines;
  const QList<QByteArray> rawLines = contents.split('\n');
  for (const QByteArray & rawLine : rawLines) {
    lines.push_back(QString::fromUtf8(rawLine).trimmed());
  }
  return lines;
}

/*
 * Split a line of the form "{name}{original name}{command}{preview command}{default values...}"
 * (the first and last characters are the outer braces).
 */
bool parseBracedLine(const QString & line, QList<QString> & fields, bool oldNewlineCharacter)
{
  if (line.size() < 2) {
    return false;
  }
  fields = line.mid(1, line.size() - 2).split("}{");
  for (QString & str : fields) {
    str.replace(QChar(gmic_lbrace), QString("{"));
    str.replace(QChar(gmic_rbrace), QString("}"));
    if (oldNewlineCharacter) {
      // (29 == gmic_newline) until gmic version 2.7.1
      str.replace(QChar(29), QString("\n"));
    }
  }
  return true;
}

bool buildFave(QList<QString> fields, bool fromGmicGTK, FavesModel::Fave & fave)
{
  if (fields.size() < 4) {
    return false;
  }
  QString command = fields[2];
  QString previewCommand = fields[3];
  if (fromGmicGTK) {
    if (command.startsWith("gimp_")) {
      command = QString("fx_") + command.mid(5);
    }
    if (previewCommand.startsWith("gimp_")) {
      previewCommand = QString("fx_") + previewCommand.mid(5);
    }
  }
  // A fave without name or command cannot be displayed nor run
  if (fields[0].isEmpty() || command.isEmpty()) {
    return false;
  }
  fave.setName(fields[0]);
  fave.setOriginalName(fields[1]);
  fave.setCommand(command);
  fave.setPreviewCommand(previewCommand);
  fields.erase(fields.begin(), fields.begin() + 4);
  fave.setDefaultValues(fields);
  fave.build();
  return true;
}

} // namespace

FavesModelReader::FavesModelReader(FavesModel & model) : _model(model) {}

bool FavesModelReader::gmicGTKFaveFileAvailable()
{
  QFileInfo info(gmicGTKFavesFilename());
  return info.isReadable();
}

FavesModel::Fave FavesModelReader::jsonObjectToFave(const QJsonObject & object)
{
  FavesModel::Fave fave;
  fave.setName(object.value("Name").toString(""));
  fave.setOriginalName(object.value("originalName").toString(""));
  fave.setCommand(object.value("command").toString(""));
  fave.setPreviewCommand(object.value("preview").toString());
  QStringList defaultParameters;
  const QJsonArray parametersArray = object.value("defaultParameters").toArray();
  for (const QJsonValue & value : parametersArray) {
    defaultParameters.push_back(value.toString());
  }
  fave.setDefaultValues(defaultParameters);
  QList<int> defaultVisibilities;
  const QJsonArray visibilitiesArray = object.value("defaultVisibilities").toArray();
  for (const QJsonValue & value : visibilitiesArray) {
    defaultVisibilities.push_back(value.toInt());
  }
  fave.setDefaultVisibilities(defaultVisibilities);
  fave.build();
  return fave;
}

void FavesModelReader::importFavesFromGmicGTK()
{
  const QString filename = gmicGTKFavesFilename();
  QFile file(filename);
  if (!file.open(QIODevice::ReadOnly)) {
    Logger::error("Import failed. Cannot open " + filename);
    return;
  }
  QByteArray contents;
  if (!readFileContents(file, contents)) {
    return;
  }
  int lineNumber = 0;
  const QList<QString> lines = splitLines(contents);
  for (const QString & line : lines) {
    ++lineNumber;
    if (line.isEmpty()) {
      continue;
    }
    QList<QString> fields;
    FavesModel::Fave fave;
    if (parseBracedLine(line, fields, false) && buildFave(fields, true, fave)) {
      _model.addFave(fave);
    } else {
      Logger::error(QString("Import failed for fave at %1:%2").arg(file.fileName()).arg(lineNumber));
    }
  }
}

void FavesModelReader::loadFaves()
{
  // Read JSON faves if file exists
  const QString jsonFilename(QString("%1%2").arg(gmicConfigPath(false)).arg("gmic_qt_faves.json"));
  QFile jsonFile(jsonFilename);
  if (jsonFile.exists()) {
    QByteArray contents;
    if (!jsonFile.open(QIODevice::ReadOnly)) {
      Logger::log("Faves loading failed: Cannot open " + jsonFilename);
      return;
    }
    if (!readFileContents(jsonFile, contents)) {
      return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
      Logger::error("Cannot load faves (parse error) : " + jsonFilename);
      Logger::error(parseError.errorString());
      return;
    }
    if (!document.isArray()) {
      Logger::error("Cannot load faves (a JSON array was expected) : " + jsonFilename);
      return;
    }
    const QJsonArray array = document.array();
    int index = 0;
    for (const QJsonValue & value : array) {
      ++index;
      if (!value.isObject()) {
        Logger::warning(QString("Faves: ignoring entry #%1 which is not a JSON object (%2)").arg(index).arg(jsonFilename));
        continue;
      }
      const FavesModel::Fave fave = jsonObjectToFave(value.toObject());
      if (fave.name().isEmpty() || fave.command().isEmpty()) {
        Logger::warning(QString("Faves: ignoring entry #%1 which has no name or no command (%2)").arg(index).arg(jsonFilename));
        continue;
      }
      _model.addFave(fave);
    }
    return;
  }

  // Read old 2.0.0 prerelease file format if no JSON was found
  const QString filename(QString("%1%2").arg(gmicConfigPath(false)).arg("gmic_qt_faves"));
  QFile file(filename);
  if (!file.exists()) {
    return;
  }
  if (!file.open(QIODevice::ReadOnly)) {
    Logger::error("Fave loading failed. Cannot open " + filename);
    return;
  }
  QByteArray contents;
  if (!readFileContents(file, contents)) {
    return;
  }
  int lineNumber = 0;
  const QList<QString> lines = splitLines(contents);
  for (const QString & line : lines) {
    ++lineNumber;
    if (!line.startsWith("{")) {
      continue;
    }
    QList<QString> fields;
    FavesModel::Fave fave;
    if (parseBracedLine(line, fields, true) && buildFave(fields, false, fave)) {
      _model.addFave(fave);
    } else {
      Logger::log(QString("Loading failed for fave at %1:%2").arg(file.fileName()).arg(lineNumber));
    }
  }
}

QString FavesModelReader::gmicGTKFavesFilename()
{
  return QString("%1%2").arg(gmicConfigPath(false)).arg("gimp_faves");
}

} // namespace GmicQt
