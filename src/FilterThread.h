/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file FilterThread.h
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
#ifndef GMIC_QT__FILTERTHREAD_H
#define GMIC_QT__FILTERTHREAD_H

#include <QElapsedTimer>
#include <QList>
#include <QString>
#include <QStringList>
#include <QThread>
#include <memory>
#include "Common.h"
#include "GmicQt.h"
#include "Host/GmicQtHost.h"

namespace gmic_library
{
template <typename T> struct gmic_list;
template <typename T> struct gmic_image;
} // namespace gmic_library

namespace GmicQt
{

class FilterThread : public QThread {
  Q_OBJECT

public:
  FilterThread(QObject * parent, const QString & command, const QString & arguments, const QString & environment);

  ~FilterThread() override;
  void setInputImages(const gmic_library::gmic_list<float> & list);
  void setImageNames(const gmic_library::gmic_list<char> & imageNames);
  void swapImages(gmic_library::gmic_list<float> & images);
  const gmic_library::gmic_list<float> & images() const;
  const gmic_library::gmic_list<char> & imageNames() const;
  gmic_library::gmic_image<char> & persistentMemoryOutput();
  QStringList gmicStatus() const;
  QList<int> parametersVisibilityStates() const;
  QString errorMessage() const;
  bool failed() const;
  bool aborted() const;
  /** Elapsed time (in milliseconds) since the creation of the thread object. */
  int duration() const;
  float progress() const;
  QString fullCommand() const;
  void setLogSuffix(const QString & text);

  static QStringList status2StringList(const QString &);
  static QList<int> status2Visibilities(const QString &);

public slots:
  void abortGmic();

signals:
  void done();

protected:
  void run() override;

private:
  const QString _command;
  const QString _arguments;
  const QString _environment;
  std::unique_ptr<gmic_library::gmic_list<float>> _images;
  std::unique_ptr<gmic_library::gmic_list<char>> _imageNames;
  std::unique_ptr<gmic_library::gmic_image<char>> _persistentMemoryOutput;
  // Note: the G'MIC library API requires plain "bool *" and "float *" for the abort flag and the
  // progress value (see the gmic constructor), which are respectively written by the GUI thread and
  // by the filter thread. They cannot be std::atomic objects.
  bool _gmicAbort = false;
  bool _failed = false;
  QString _gmicStatus;
  float _gmicProgress = 0.0f;
  QString _errorMessage;
  QString _logSuffix;
  QElapsedTimer _startTime;
};

} // namespace GmicQt

#endif // GMIC_QT__FILTERTHREAD_H
