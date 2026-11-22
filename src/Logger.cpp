/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file Logger.cpp
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
#include "Logger.h"
#include <QByteArray>
#include <QDebug>
#include <QString>
#include <QStringList>
#include <cstdio>
#include <mutex>
#include "Common.h"
#include "GmicQt.h"
#include "Settings.h"
#include "Utils.h"
#include "gmic.h"

namespace GmicQt
{

namespace
{

// Logging may be requested from the filter threads as well as from the GUI thread, while the
// GUI thread may close or reopen the log file. All accesses to the log file go through this mutex.
// It is recursive because the public functions call each other.
std::recursive_mutex & loggerMutex()
{
  static std::recursive_mutex mutex;
  return mutex;
}

// Empty if the G'MIC configuration folder does not exist and cannot be created. In that case we must
// not build a relative file name: the log file would be created in the current working directory.
QString logFilePath()
{
  const QString folder = gmicConfigPath(true);
  return folder.isEmpty() ? QString() : QString("%1gmic_qt_log").arg(folder);
}

} // namespace

FILE * Logger::_logFile = nullptr;
Logger::Mode Logger::_currentMode = Logger::Mode::StandardOutput;

void Logger::setMode(const OutputMessageMode mode)
{
  if ((mode == OutputMessageMode::VerboseLogFile) || (mode == OutputMessageMode::VeryVerboseLogFile) || (mode == OutputMessageMode::DebugLogFile)) {
    setMode(Logger::Mode::File);
  } else {
    setMode(Logger::Mode::StandardOutput);
  }
}

void Logger::setMode(const Logger::Mode mode)
{
  std::lock_guard<std::recursive_mutex> lock(loggerMutex());
  if (mode == _currentMode) {
    return;
  }
  if (mode == Mode::StandardOutput) {
    // Redirect the output first: closing the file while CImg still points to it would leave a dangling FILE *.
    gmic_library::cimg::output(stdout);
    if (_logFile) {
      fclose(_logFile);
      _logFile = nullptr;
    }
    _currentMode = Mode::StandardOutput;
    return;
  }
  const QString filename = logFilePath();
  if (filename.isEmpty()) {
    return; // Keep logging on the standard output (and allow a later retry)
  }
  FILE * file = fopen(filename.toLocal8Bit().constData(), "a");
  if (!file) {
    return; // Keep logging on the standard output (and allow a later retry)
  }
  _logFile = file;
  gmic_library::cimg::output(_logFile);
  _currentMode = Mode::File;
}

void Logger::close()
{
  setMode(Mode::StandardOutput);
}

void Logger::clear()
{
  std::lock_guard<std::recursive_mutex> lock(loggerMutex());
  const Mode mode = _currentMode;
  if (mode == Mode::File) {
    setMode(Mode::StandardOutput);
  }
  const QString filename = logFilePath();
  if (!filename.isEmpty()) {
    FILE * truncatedFile = fopen(filename.toLocal8Bit().constData(), "w");
    if (truncatedFile) {
      fclose(truncatedFile);
    }
  }
  setMode(mode);
}

void Logger::log(const QString & message, bool space)
{
  log(message, QString(), space);
}

void Logger::log(const QString & message, const QString & hint, bool space)
{
  if (Settings::outputMessageMode() == OutputMessageMode::Quiet) {
    return;
  }
  QString text = message;
  while (!text.isEmpty() && text[text.size() - 1].isSpace()) {
    text.chop(1);
  }
  const QStringList lines = text.split("\n", QT_KEEP_EMPTY_PARTS);

  QString prefix = QString("[%1]").arg(pluginCodeName());
  prefix += hint.isEmpty() ? " " : QString("./%1/ ").arg(hint);

  // Build the whole text first, so that it is written at once (messages coming from
  // different threads are not interleaved line by line).
  QByteArray output;
  if (space) {
    output.append('\n');
  }
  for (const QString & line : lines) {
    output.append((prefix + line).toLocal8Bit());
    output.append('\n');
  }

  std::lock_guard<std::recursive_mutex> lock(loggerMutex());
  FILE * file = gmic_library::cimg::output();
  if (!file) {
    file = stdout;
  }
  std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), file);
  std::fflush(file);
}

void Logger::error(const QString & message, bool space)
{
  log(message, "error", space);
}

void Logger::warning(const QString & message, bool space)
{
  log(message, "warning", space);
}

void Logger::note(const QString & message, bool space)
{
  log(message, "note", space);
}

} // namespace GmicQt
