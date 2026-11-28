/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file FilterSyncRunner.cpp
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
#include "FilterSyncRunner.h"
#include <QDebug>
#include <QByteArray>
#include <QThread>
#include <exception>
#include <iostream>
#include <new>
#include "FilterThread.h"
#include "GmicStdlib.h"
#include "Logger.h"
#include "Misc.h"
#include "PersistentMemory.h"
#include "Settings.h"
#include "gmic.h"

namespace GmicQt
{

FilterSyncRunner::FilterSyncRunner(QObject * parent, const QString & command, const QString & arguments, const QString & environment)
    : QObject(parent), _command(command), _arguments(arguments), _environment(environment), //
      _images(new gmic_library::gmic_list<float>),                                          //
      _imageNames(new gmic_library::gmic_list<char>),                                       //
      _persistentMemoryOutput(new gmic_library::gmic_image<char>)
{
#ifdef _IS_MACOS_
  static bool stackSize8MB = false;
  if (!stackSize8MB) {
    QThread::currentThread()->setStackSize(8 * 1024 * 1024);
    stackSize8MB = true;
  }
#endif
}

// Defined here, where the types of the owned objects are complete
FilterSyncRunner::~FilterSyncRunner() = default;

void FilterSyncRunner::setArguments(const QString & str)
{
  _arguments = str;
}

void FilterSyncRunner::setImageNames(const gmic_library::gmic_list<char> & imageNames)
{
  *_imageNames = imageNames;
}

void FilterSyncRunner::swapImages(gmic_library::gmic_list<float> & images)
{
  _images->swap(images);
}

void FilterSyncRunner::setInputImages(const gmic_library::gmic_list<float> & list)
{
  *_images = list;
}

const gmic_library::gmic_list<float> & FilterSyncRunner::images() const
{
  return *_images;
}

const gmic_library::gmic_list<char> & FilterSyncRunner::imageNames() const
{
  return *_imageNames;
}

gmic_library::gmic_image<char> & FilterSyncRunner::persistentMemoryOutput()
{
  return *_persistentMemoryOutput;
}

QStringList FilterSyncRunner::gmicStatus() const
{
  return FilterThread::status2StringList(_gmicStatus);
}

QList<int> FilterSyncRunner::parametersVisibilityStates() const
{
  return FilterThread::status2Visibilities(_gmicStatus);
}

QString FilterSyncRunner::errorMessage() const
{
  return _errorMessage;
}

bool FilterSyncRunner::failed() const
{
  return _failed;
}

bool FilterSyncRunner::aborted() const
{
  float value;
  cimg_pragma_openmp(atomic read)
  value = _gmicAbort;
  return value;
}

float FilterSyncRunner::progress() const
{
  float value;
  cimg_pragma_openmp(atomic read)
  value = _gmicProgress;
  return value;
}

QString FilterSyncRunner::fullCommand() const
{
  QString result = _command;
  appendWithSpace(result, _arguments);
  return result;
}

void FilterSyncRunner::setLogSuffix(const QString & text)
{
  _logSuffix = text;
}

void FilterSyncRunner::abortGmic()
{
  cimg_pragma_openmp(atomic write)
  _gmicAbort = true;
}

void FilterSyncRunner::run()
{
  _errorMessage.clear();
  _failed = false;
  QString fullCommandLine;
  // This function is the body of a worker thread: no exception may escape from it (that would
  // terminate the whole host application). Any failure is reported through failed() and errorMessage().
  auto fail = [this, &fullCommandLine](const QString & message) {
    _images->assign();
    _imageNames->assign();
    _errorMessage = message;
    Logger::error(QString("When running command '%1', this error occurred:\n%2").arg(fullCommandLine, message), true);
    _failed = true;
  };
  try {
    fullCommandLine = commandFromOutputMessageMode(Settings::outputMessageMode());
    appendWithSpace(fullCommandLine, _command);
    appendWithSpace(fullCommandLine, _arguments);
    cimg_pragma_openmp(atomic write)
    _gmicAbort = false;
    cimg_pragma_openmp(atomic write)
    _gmicProgress = -1;
    Logger::log(fullCommandLine, _logSuffix, true);
    const QByteArray environment = _environment.toLocal8Bit(); // Must outlive the construction of the interpreter
    gmic gmicInstance(_environment.isEmpty() ? nullptr : environment.constData(), GmicStdLib::Array.constData(), true, &_gmicProgress, &_gmicAbort, 0.0f);
    if (PersistentMemory::image()) {
      if (*PersistentMemory::image() == gmic_store) {
        gmicInstance.set_variable("_persistent", PersistentMemory::image());
      } else {
        gmicInstance.set_variable("_persistent", '=', PersistentMemory::image());
      }
    }
    gmicInstance.set_variable("_host", '=', GmicQtHost::ApplicationShortname);
    gmicInstance.set_variable("_tk", '=', "qt");
    const QByteArray commandLine = fullCommandLine.toLocal8Bit();
    gmicInstance.run(commandLine.constData(), *_images, *_imageNames);
    _gmicStatus = QString::fromLocal8Bit(gmicInstance.status);
    gmicInstance.get_variable("_persistent").move_to(*_persistentMemoryOutput);
  } catch (gmic_exception & e) {
    fail(QString::fromUtf8(e.what()));
  } catch (std::bad_alloc &) {
    fail(QString("Not enough memory"));
  } catch (std::exception & e) { // e.g. CImg exceptions
    fail(QString::fromLocal8Bit(e.what()));
  } catch (...) {
    fail(QString("Unknown error"));
  }
}

} // namespace GmicQt
