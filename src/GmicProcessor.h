/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file GmicProcessor.h
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
#ifndef GMIC_QT_GMICPROCESSOR_H
#define GMIC_QT_GMICPROCESSOR_H

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <deque>
#include <memory>
#include "GmicQt.h"
#include "InputOutputState.h"

namespace gmic_library
{
template <typename T> struct gmic_list;
template <typename T> struct gmic_image;
} // namespace gmic_library

namespace GmicQt
{
class FilterThread;
class FilterSyncRunner;

class GmicProcessor : public QObject {
  Q_OBJECT
public:
  /**
   * Every member has a default value: a context which is only partially filled by the caller
   * (e.g. a full image request does not set the preview related fields) must never lead to the
   * reading of an indeterminate value.
   */
  struct FilterContext {
    enum class RequestType
    {
      SynchronousPreview,
      Preview,
      FullImage,
      GUIDynamismRun
    };
    struct VisibleRect {
      double x = 0.0;
      double y = 0.0;
      double w = 1.0;
      double h = 1.0;
    };
    struct PositionStringCorrection {
      double xFactor = 1.0;
      double yFactor = 1.0;
    };
    RequestType requestType = RequestType::Preview;
    VisibleRect visibleRect;
    InputOutputState inputOutputState;
    PositionStringCorrection positionStringCorrection;
    double zoomFactor = 1.0;
    int previewWindowWidth = 0;
    int previewWindowHeight = 0;
    int previewTimeout = 0;
    bool previewFromFullImage = false;
    bool previewCheckBox = false;
    bool randomized = false;
    QString filterName;
    QString filterCommand;
    QString filterFullPath;
    QString filterArguments;
    QString filterHash;
  };

  explicit GmicProcessor(QObject * parent = nullptr);
  ~GmicProcessor() override;
  GmicProcessor(const GmicProcessor &) = delete;
  GmicProcessor & operator=(const GmicProcessor &) = delete;

  void init();
  void setContext(const FilterContext & context);
  void execute();

  bool isProcessingFullImage() const;
  bool isProcessing() const;
  bool isIdle() const;
  bool hasUnfinishedAbortedThreads() const;

  const gmic_library::gmic_image<float> & previewImage() const;
  const QStringList & gmicStatus() const;
  const QList<int> & parametersVisibilityStates() const;
  void setGmicStatusQuotedParameters(const QVector<bool> & quotedParameters);

  void saveSettings(QSettings & settings);

  int duration() const;
  float progress() const;
  int lastPreviewFilterExecutionDurationMS() const;
  void resetLastPreviewFilterExecutionDurations();
  void recordPreviewFilterExecutionDurationMS(int duration);
  int averagePreviewFilterExecutionDuration() const;
  int completedFullImageProcessingCount() const;
  qint64 lastCompletedExecutionTime() const;

public slots:
  void cancel();
  void detachAllUnfinishedAbortedThreads();
  void terminateAllThreads();

signals:
  void previewCommandFailed(QString errorMessage);
  void fullImageProcessingFailed(QString errorMessage);
  void previewImageAvailable();
  void guiDynamismRunDone();
  void fullImageProcessingDone();
  void noMoreUnfinishedJobs();
  void aboutToSendImagesToHost();

private slots:
  void onPreviewThreadFinished();
  void onApplyThreadFinished();
  void onGUIDynamismThreadFinished();
  void onAbortedThreadFinished();
  void showWaitingCursor();
  void hideWaitingCursor();

private:
  void updateImageNames(gmic_library::gmic_list<char> & imageNames);
  void abortCurrentFilterThread();
  void manageSynchonousRunner(FilterSyncRunner & runner);
  /** Schedules the deletion of the current filter thread, and forgets it. */
  void releaseFilterThread();

  FilterThread * _filterThread = nullptr;
  FilterContext _filterContext;
  std::unique_ptr<gmic_library::gmic_list<float>> _gmicImages;
  std::unique_ptr<gmic_library::gmic_image<float>> _previewImage;
  QList<FilterThread *> _unfinishedAbortedThreads;

  unsigned int _previewRandomSeed = 0;
  QStringList _gmicStatus;
  QList<int> _parametersVisibilityStates;
  QTimer _waitingCursorTimer;
  static const int WAITING_CURSOR_DELAY = 200;

  QString _lastAppliedFilterPath;
  QString _lastAppliedFilterHash;
  QString _lastAppliedCommand;
  QString _lastAppliedCommandArguments;
  QStringList _lastAppliedCommandGmicStatus;
  InputOutputState _lastAppliedCommandInOutState;
  QElapsedTimer _ongoingFilterExecutionTime;
  QElapsedTimer _completedExecutionTime;
  qint64 _lastCompletedExecutionTime = 0;
  std::deque<int> _lastFilterPreviewExecutionDurations;
  int _completeFullImageProcessingCount = 0;
  QVector<bool> _gmicStatusQuotedParameters;
};

} // namespace GmicQt

#endif // GMIC_QT_GMICPROCESSOR_H
