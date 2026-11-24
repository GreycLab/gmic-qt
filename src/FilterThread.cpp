/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file FilterThread.cpp
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
#include "FilterThread.h"
#include <QDebug>
#include <QRegularExpression>
#include <algorithm>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include "FilterParameters/AbstractParameter.h"
#include "GmicStdlib.h"
#include "Logger.h"
#include "Misc.h"
#include "PersistentMemory.h"
#include "Settings.h"
#include "gmic.h"

namespace GmicQt
{

FilterThread::FilterThread(QObject * parent, const QString & command, const QString & arguments, const QString & environment)
    : QThread(parent), _command(command), _arguments(arguments), _environment(environment), //
      _images(new gmic_library::gmic_list<float>),                                          //
      _imageNames(new gmic_library::gmic_list<char>),                                       //
      _persistentMemoryOutput(new gmic_library::gmic_image<char>)
{
  // Started here, and not in run(): duration() may be called by the GUI thread before the thread
  // actually starts (elapsed() is undefined on an invalid timer), and restarting the timer from
  // another thread while duration() is reading it would be a data race.
  _startTime.start();
#ifdef _IS_MACOS_
  setStackSize(8 * 1024 * 1024);
#endif
}

FilterThread::~FilterThread() = default; // Defined here, where the G'MIC types are complete

void FilterThread::setImageNames(const gmic_library::gmic_list<char> & imageNames)
{
  *_imageNames = imageNames;
}

void FilterThread::swapImages(gmic_library::gmic_list<float> & images)
{
  _images->swap(images);
}

void FilterThread::setInputImages(const gmic_library::gmic_list<float> & list)
{
  *_images = list;
}

const gmic_library::gmic_list<float> & FilterThread::images() const
{
  return *_images;
}

const gmic_library::gmic_list<char> & FilterThread::imageNames() const
{
  return *_imageNames;
}

gmic_library::gmic_image<char> & FilterThread::persistentMemoryOutput()
{
  return *_persistentMemoryOutput;
}

// Decompose a status string into list of items ('strings' or 'visibilities').
// Output list is either :
// - A list of 'strings' (terminated by null character), if output_visibility==false;
// - Or a list of 'visibilities' (single char in { 0,'0','1','2' }, **not** terminated by null character).
static gmic_list<char> status2Items(const char * status, const bool output_visibility)
{
  if (!status || (*status != gmic_lbrace)) {
    return gmic_list<char>();
  }
  const size_t length = std::strlen(status);
  if (length > static_cast<size_t>((std::numeric_limits<int>::max)() - 4)) {
    return gmic_list<char>(); // Absurdly long status: do not risk any integer overflow below
  }
  const int len = static_cast<int>(length);
  gmic_list<char> out;

  bool is_inside = false;
  int pk = 0;

  for (int k = 0; k < len; ++k) {
    const char c = status[k];
    switch (c) {
    case gmic_lbrace:
      if (!is_inside) {
        if (k >= len - 1) {
          return gmic_list<char>();
        }
        is_inside = true;
        pk = k + 1;
      }
      break;
    case gmic_rbrace: {
      if (!is_inside) {
        return gmic_list<char>();
      }
      is_inside = false;
      char visibility = 0;
      const int ck = k;
      if ((k < len - 2) && (status[k + 1] == '_') && (status[k + 2] >= '0') && (status[k + 2] <= '2')) {
        k += 2;
        visibility = status[k];
      }
      if (output_visibility) {
        gmic_image<char>(1, 1, 1, 1, visibility).move_to(out);
      } else {
        // ck >= pk here, so the item has (ck - pk) characters, plus a terminating null character
        gmic_image<char> item(static_cast<unsigned int>(ck - pk + 1));
        item.back() = 0;
        std::memcpy(item.data(), status + pk, static_cast<size_t>(item.width() - 1));
        gmic::strreplace_fw(item);
        item.move_to(out);
      }
    } break;
    default:
      if (!is_inside) {
        return gmic_list<char>();
      }
    }
  }
  return out;
}

QStringList FilterThread::status2StringList(const QString & status)
{
  const QByteArray ba = status.toLocal8Bit();
  const gmic_list<char> clist = status2Items(ba.constData(), false);
  QStringList qlist;
  for (unsigned int l = 0; l < clist.size(); ++l) {
    const char * item = clist[l].data();
    // Empty item "{}": no value provided, the parameter must keep its current value.
    // It is represented by a null QString.
    qlist << ((item && *item) ? QString::fromLocal8Bit(item) : QString());
  }
  return qlist;
}

QList<int> FilterThread::status2Visibilities(const QString & status)
{
  const QByteArray ba = status.toLocal8Bit();
  const gmic_list<char> clist = status2Items(ba.constData(), true);
  QList<int> qlist;
  for (unsigned int l = 0; l < clist.size(); ++l) {
    const char c = clist(l, 0);
    qlist.push_back(c ? (c - '0') : static_cast<int>(AbstractParameter::VisibilityState::Unspecified));
  }
  return qlist;
}

QStringList FilterThread::gmicStatus() const
{
  return status2StringList(_gmicStatus);
}

QList<int> FilterThread::parametersVisibilityStates() const
{
  return status2Visibilities(_gmicStatus);
}

QString FilterThread::errorMessage() const
{
  return _errorMessage;
}

bool FilterThread::failed() const
{
  return _failed;
}

bool FilterThread::aborted() const
{
  return _gmicAbort;
}

int FilterThread::duration() const
{
  const qint64 elapsed = _startTime.elapsed();
  const qint64 maxInt = (std::numeric_limits<int>::max)();
  return static_cast<int>(std::max<qint64>(0, std::min<qint64>(elapsed, maxInt)));
}

float FilterThread::progress() const
{
  return _gmicProgress;
}

QString FilterThread::fullCommand() const
{
  QString result = _command;
  appendWithSpace(result, _arguments);
  return result;
}

void FilterThread::setLogSuffix(const QString & text)
{
  _logSuffix = text;
}

void FilterThread::abortGmic()
{
  _gmicAbort = true;
}

void FilterThread::run()
{
  _errorMessage.clear();
  _failed = false;
  QString fullCommandLine;
  try {
    fullCommandLine = commandFromOutputMessageMode(Settings::outputMessageMode());
    appendWithSpace(fullCommandLine, _command);
    appendWithSpace(fullCommandLine, _arguments);
    // Note: _gmicAbort is initialized by the constructor and must not be reset here, otherwise an
    // abortGmic() call made between start() and the actual execution of run() would be lost.
    _gmicProgress = -1;
    Logger::log(fullCommandLine, _logSuffix, true);
    // These buffers must outlive the calls below which use their content
    const QByteArray environment = _environment.toLocal8Bit();
    const QByteArray commandLine = fullCommandLine.toLocal8Bit();
    if (_gmicAbort) { // Aborted before the start: skip the (costly) initialization of the interpreter
      _images->assign();
      _imageNames->assign();
      return;
    }
    gmic gmicInstance(environment.isEmpty() ? nullptr : environment.constData(), GmicStdLib::Array.constData(), true, &_gmicProgress, &_gmicAbort, 0.0f);
    if (_gmicAbort) { // Aborted during the initialization
      _images->assign();
      _imageNames->assign();
      return;
    }
    if (PersistentMemory::image()) {
      if (*PersistentMemory::image() == gmic_store) {
        gmicInstance.set_variable("_persistent", PersistentMemory::image());
      } else {
        gmicInstance.set_variable("_persistent", '=', PersistentMemory::image());
      }
    }
    gmicInstance.set_variable("_host", '=', GmicQtHost::ApplicationShortname);
    gmicInstance.set_variable("_tk", '=', "qt");
    gmicInstance.run(commandLine.constData(), *_images, *_imageNames);
    _gmicStatus = QString::fromLocal8Bit(gmicInstance.status);
    gmicInstance.get_variable("_persistent").move_to(*_persistentMemoryOutput);
  } catch (gmic_exception & e) {
    _images->assign();
    _imageNames->assign();
    const char * message = e.what();
    _errorMessage = message;
    Logger::error(QString("When running command '%1', this error occurred:\n%2").arg(fullCommandLine).arg(message), true);
    _failed = true;
  } catch (std::exception & e) {
    // An exception escaping from run() would terminate the whole host application.
    _images->assign();
    _imageNames->assign();
    _errorMessage = e.what();
    Logger::error(QString("When running command '%1', this error occurred:\n%2").arg(fullCommandLine).arg(_errorMessage), true);
    _failed = true;
  } catch (...) {
    _images->assign();
    _imageNames->assign();
    _errorMessage = "Unknown error";
    Logger::error(QString("When running command '%1', an unknown error occurred.").arg(fullCommandLine), true);
    _failed = true;
  }
}

} // namespace GmicQt



