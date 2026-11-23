/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file TimeLogger.cpp
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

#include "TimeLogger.h"
#include <QDebug>
#include <QString>
#include <mutex>
#include "Common.h"
#include "Utils.h"
#include "gmic.h"

namespace GmicQt
{

namespace
{
// The instance and the step counters are shared by all the threads that use the TIMING macro.
std::mutex & timeLoggerMutex()
{
  static std::mutex mutex;
  return mutex;
}
} // namespace

std::unique_ptr<TimeLogger> TimeLogger::_instance = nullptr;

TimeLogger::TimeLogger()
{
  // Without configuration folder, do not create the file in the current folder: timings are only printed.
  const QString folder = gmicConfigPath(true);
  if (!folder.isEmpty()) {
    const QString filename = folder + "timing_log.txt";
    _file = fopen(filename.toLocal8Bit().constData(), "w");
  }
  Q_ASSERT_X(_file, __PRETTY_FUNCTION__, "Cannot open log file");
}

TimeLogger::~TimeLogger()
{
  if (_file) {
    fclose(_file);
  }
}

TimeLogger * TimeLogger::getInstance()
{
  std::lock_guard<std::mutex> lock(timeLoggerMutex());
  if (!_instance) {
    _instance = std::unique_ptr<TimeLogger>(new TimeLogger);
  }
  return _instance.get();
}

void TimeLogger::step(const char * function, int line, const char * filename)
{
  std::lock_guard<std::mutex> lock(timeLoggerMutex());
  static cimg_ulong first = 0;
  static cimg_ulong last = 0;
  static unsigned int count = 0;
  const cimg_ulong now = gmic_library::cimg::time();
  if (!last) {
    last = first = now;
  }
  // Computed with doubles: the system clock may go backward, which would wrap around with unsigned integers.
  const double elapsed = (static_cast<double>(now) - static_cast<double>(last)) / 1000.0;
  const double total = (static_cast<double>(now) - static_cast<double>(first)) / 1000.0;
  const char * functionName = function ? function : "?";
  const char * fileName = filename ? filename : "?";
  printf("%02u @%2.3f +%2.3f %s <%s:%d>\n", count, total, elapsed, functionName, fileName, line);
  if (_file) {
    fprintf(_file, "%02u @%2.3f +%2.3f %s <%s:%d>\n", count, total, elapsed, functionName, fileName, line);
  }
  ++count;
  last = now;
}

} // namespace GmicQt



