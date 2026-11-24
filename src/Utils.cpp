/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file Utils.cpp
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

#include "Utils.h"
#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <algorithm>
#include <limits>
#include "Common.h"
#include "Host/GmicQtHost.h"
#include "Logger.h"
#include "gmic.h"

#ifdef _IS_WINDOWS_
#include <windows.h>
#include <cstring>
#include <tlhelp32.h>
#include <psapi.h>
#endif
#ifdef _IS_UNIX_
#include <unistd.h>
#endif

#ifdef _IS_UNIX_
namespace
{
/**
 * Looks for the "VmRSS:" entry in the content of /proc/self/status and reads the (unsigned)
 * number which follows. Works on a bounded buffer, and reports any failure.
 *
 * It replaces strstr() + sscanf(str + 7, ...) where sscanf() returns EOF (a non-zero value) when
 * no number is found, and where "str + 7" could point past the end of the buffer.
 */
bool parseVmRssKiB(const char * text, const int size, quint64 & kiB)
{
  static const char key[] = "VmRSS:";
  const int keyLength = static_cast<int>(sizeof(key)) - 1;
  if (!text || (size < keyLength)) {
    return false;
  }
  const char * const end = text + size;
  const char * found = std::search(text, end, key, key + keyLength);
  if (found == end) {
    return false;
  }
  const char * p = found + keyLength;
  while ((p != end) && ((*p == ' ') || (*p == '\t'))) {
    ++p;
  }
  const char * const digits = p;
  quint64 value = 0;
  while ((p != end) && (*p >= '0') && (*p <= '9')) {
    const unsigned int digit = static_cast<unsigned int>(*p - '0');
    if (value > (std::numeric_limits<quint64>::max() - digit) / 10) {
      return false; // Overflow
    }
    value = value * 10 + digit;
    ++p;
  }
  if (p == digits) {
    return false;
  }
  kiB = value;
  return true;
}
} // namespace
#endif

namespace GmicQt
{

const QString & gmicConfigPath(bool create)
{
#if defined(Q_OS_ANDROID)
  QString baseAppPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  const QString qpath = QString::fromUtf8(gmic::path_rc(qPrintable(baseAppPath)));
#elif defined(_IS_WINDOWS_)
  const QString qpath = QString::fromUtf8(gmic::path_rc());
#else
  const QString qpath = QString::fromLocal8Bit(gmic::path_rc());
#endif
  static QString result;
  static QMutex mutex;
  QFileInfo pathInfo(qpath);

  if (create) gmic::init_rc();
  const QString newValue = pathInfo.isDir() ? qpath : QString();
  QMutexLocker locker(&mutex);
  // The returned reference is shared: avoid reassigning it when the value is unchanged,
  // so that concurrent readers (e.g. logging from other threads) are not disturbed.
  if (result != newValue) {
    result = newValue;
  }
  return result;
}

unsigned int host_app_pid()
{
#if defined(_IS_UNIX_)
  return static_cast<unsigned int>(getppid());
#elif defined(_IS_WINDOWS_)
  const DWORD pid = GetCurrentProcessId();
  HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (h == INVALID_HANDLE_VALUE) {
    return static_cast<unsigned int>(pid); // Process own id if the snapshot could not be taken
  }
  PROCESSENTRY32 pe;
  memset(&pe, 0, sizeof(PROCESSENTRY32));
  pe.dwSize = sizeof(PROCESSENTRY32);
  if (Process32First(h, &pe)) {
    do {
      if (pe.th32ProcessID == pid) {
        CloseHandle(h);
        return static_cast<unsigned int>(pe.th32ParentProcessID);
      }
    } while (Process32Next(h, &pe));
  }
  CloseHandle(h);
  return static_cast<unsigned int>(pid); // Process own id if no parent was found
#else
  return 0;
#endif
}

const QString & pluginFullName()
{
#ifdef gmic_prerelease
#define BETA_SUFFIX "_pre#" gmic_prerelease
#else
#define BETA_SUFFIX ""
#endif
  static QString result;
  if (result.isEmpty()) {
    result = QString("G'MIC-Qt %1- %2 %3 bits - %4" BETA_SUFFIX)
                 .arg(GmicQtHost::ApplicationName.isEmpty() ? QString() : QString("for %1 ").arg(GmicQtHost::ApplicationName))
                 .arg(gmic_library::cimg::stros())
                 .arg(sizeof(void *) == 8 ? 64 : 32)
                 .arg(gmicVersionString());
  }
  return result;
}

const QString & pluginCodeName()
{
  static QString result;
  if (result.isEmpty()) {
    result = GmicQtHost::ApplicationName.isEmpty() ? QString("gmic_qt") : QString("gmic_%1_qt").arg(QString(GmicQtHost::ApplicationShortname).toLower());
  }
  return result;
}

bool touchFile(const QString & path)
{
  QFile file(path);
  // ReadWrite creates the file if it does not exist, and does not truncate an existing one
  if (!file.open(QFile::ReadWrite)) {
    return false;
  }
  // Only the modification time is changed: the content of the file is never touched
  if (file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime)) {
    return true;
  }
  // Fallback for file systems which cannot set the time: grow then shrink the file, which updates it
  const qint64 size = file.size();
  return file.resize(size + 1) && file.resize(size);
}

bool writeAll(const QByteArray & array, QFile & file)
{
  const qint64 total = array.size();
  qint64 totalWritten = 0;
  const qint64 maxBytesPerWrite = 10ll << 20;
  const char * data = array.constData();
  while (totalWritten < total) {
    const qint64 writtenByCall = file.write(data + totalWritten, std::min(total - totalWritten, maxBytesPerWrite));
    if (writtenByCall <= 0) { // -1 on error; 0 would make no progress (infinite loop otherwise)
      Logger::error(QString("Could not properly write file %1 (%2/%3 bytes written)") //
                        .arg(file.fileName())
                        .arg(totalWritten)
                        .arg(total));
      return false;
    }
    totalWritten += writtenByCall;
  }
  if (!file.flush()) {
    Logger::error(QString("Could not flush file %1 (%2 bytes written)").arg(file.fileName()).arg(totalWritten));
    return false;
  }
  return true;
}

bool safelyWrite(const QByteArray & array, const QString & filename)
{
  QString directory = QFileInfo(filename).absoluteDir().absolutePath();
  if (!QFileInfo(directory).isWritable()) {
    Logger::error(QString("Folder is not writable (%1)").arg(directory));
    return false;
  }
  // QSaveFile writes to a temporary file located in the same folder, and only replaces the
  // destination file on commit(): the original file is left untouched if anything fails.
  QSaveFile file(filename);
  if (!file.open(QIODevice::WriteOnly)) {
    Logger::error(QString("Could not open file %1 for writing (%2)").arg(filename, file.errorString()));
    return false;
  }
  if (file.write(array) != array.size() || !file.commit()) {
    Logger::error(QString("Could not properly write file %1 (%2)").arg(filename, file.errorString()));
    file.cancelWriting();
    return false;
  }
  return true;
}

bool processMemoryUsage(quint64 & bytes)
{
#if defined(_IS_UNIX_)
  QFile status("/proc/self/status");
  if (!status.open(QFile::ReadOnly)) {
    return false;
  }
  const QByteArray text = status.readAll();
  quint64 kiB = 0;
  if (!parseVmRssKiB(text.constData(), text.size(), kiB) || (kiB > std::numeric_limits<quint64>::max() / 1024)) {
    return false;
  }
  bytes = kiB * 1024;
  return true;
#elif defined(_IS_WINDOWS_)
  PROCESS_MEMORY_COUNTERS counters = {};
  counters.cb = sizeof(counters);
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
    return false;
  }
  bytes = static_cast<quint64>(counters.WorkingSetSize);
  return true;
#else
  Q_UNUSED(bytes)
  return false;
#endif
}

} // namespace GmicQt
