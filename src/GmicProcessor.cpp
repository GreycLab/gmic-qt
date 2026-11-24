/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file GmicProcessor.cpp
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

#include "GmicProcessor.h"
#include <QApplication>
#include <QDebug>
#include <QPainter>
#include <QRegularExpression>
#include <QSize>
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include "CroppedActiveLayerProxy.h"
#include "CroppedImageListProxy.h"
#include "FilterGuiDynamismCache.h"
#include "FilterSyncRunner.h"
#include "FilterThread.h"
#include "Globals.h"
#include "Host/GmicQtHost.h"
#include "ImageTools.h"
#include "LayersExtentProxy.h"
#include "Logger.h"
#include "Misc.h"
#include "OverrideCursor.h"
#include "PersistentMemory.h"
#include "Settings.h"
#include "gmic.h"

namespace
{
/**
 * Converts a double to an int without undefined behavior: NaN gives 0, and out of range values
 * are clamped (a plain static_cast<int> is undefined when the value cannot be represented).
 */
int clampedToInt(const double value)
{
  if (std::isnan(value)) {
    return 0;
  }
  if (value >= static_cast<double>((std::numeric_limits<int>::max)())) {
    return (std::numeric_limits<int>::max)();
  }
  if (value <= static_cast<double>((std::numeric_limits<int>::min)())) {
    return (std::numeric_limits<int>::min)();
  }
  return static_cast<int>(value);
}

/** Converts a duration in milliseconds to an int, clamping instead of wrapping around. */
int clampedMilliseconds(const qint64 value)
{
  if (value > (std::numeric_limits<int>::max)()) {
    return (std::numeric_limits<int>::max)();
  }
  if (value < 0) {
    return 0;
  }
  return static_cast<int>(value);
}
} // namespace

namespace GmicQt
{

GmicProcessor::GmicProcessor(QObject * parent)
    : QObject(parent),                                                       //
      _gmicImages(new gmic_library::gmic_list<gmic_pixel_type>),             //
      _previewImage(new gmic_library::gmic_image<float>),                    //
      _lastAppliedCommandInOutState(InputOutputState::Unspecified)
{
  _waitingCursorTimer.setSingleShot(true);
  connect(&_waitingCursorTimer, &QTimer::timeout, this, &GmicProcessor::showWaitingCursor);
  gmic_library::cimg::srand();
  _previewRandomSeed = gmic_library::cimg::_rand();
  _ongoingFilterExecutionTime.start();
}

void GmicProcessor::init()
{
  abortCurrentFilterThread();
  _gmicImages->assign();
}

GmicProcessor::~GmicProcessor()
{
  _waitingCursorTimer.stop();
  // A QThread which is still running must never be destroyed (this is what happens to the children
  // of a QObject when the parent is destroyed): the current thread, if any, is first moved to the
  // list of aborted ones, then every unfinished thread is detached from this object.
  abortCurrentFilterThread();
  if (!_unfinishedAbortedThreads.isEmpty()) {
    Logger::error(QString("~GmicProcessor(): There are %1 unfinished filter threads.").arg(_unfinishedAbortedThreads.size()));
    detachAllUnfinishedAbortedThreads();
  }
}

void GmicProcessor::setContext(const GmicProcessor::FilterContext & context)
{
  _filterContext = context;
}

void GmicProcessor::execute()
{
  // Never lose track of a running thread (callers are expected to call init() first)
  abortCurrentFilterThread();

  gmic_list<char> imageNames;
  FilterContext::VisibleRect & rect = _filterContext.visibleRect;
  _gmicImages->assign();
  if ((_filterContext.requestType == FilterContext::RequestType::Preview) ||            //
      (_filterContext.requestType == FilterContext::RequestType::SynchronousPreview) || //
      (_filterContext.requestType == FilterContext::RequestType::GUIDynamismRun)) {
    if (_filterContext.previewFromFullImage) {
      CroppedImageListProxy::get(*_gmicImages, imageNames, 0.0, 0.0, 1.0, 1.0, _filterContext.inputOutputState.inputMode, 1.0);
      updateImageNames(imageNames);
    } else {
      CroppedImageListProxy::get(*_gmicImages, imageNames, rect.x, rect.y, rect.w, rect.h, _filterContext.inputOutputState.inputMode, _filterContext.zoomFactor);
      updateImageNames(imageNames);
    }
  } else {
    CroppedImageListProxy::get(*_gmicImages, imageNames, rect.x, rect.y, rect.w, rect.h, _filterContext.inputOutputState.inputMode, 1.0);
  }
  _waitingCursorTimer.start(WAITING_CURSOR_DELAY);
  const InputOutputState & io = _filterContext.inputOutputState;
  QStringList envItems;
  envItems << QString("_input_layers=%1").arg(static_cast<int>(io.inputMode));
  envItems << QString("_output_mode=%1").arg(static_cast<int>(io.outputMode));
  envItems << QString("_output_messages=%1").arg(static_cast<int>(Settings::outputMessageMode()));
  if ((_filterContext.requestType == FilterContext::RequestType::Preview) || //
      (_filterContext.requestType == FilterContext::RequestType::SynchronousPreview)) {
    envItems << QString("_preview_area_width=%1").arg(_filterContext.previewWindowWidth);
    envItems << QString("_preview_area_height=%1").arg(_filterContext.previewWindowHeight);
    envItems << QString("_preview_timeout=%1").arg(_filterContext.previewTimeout);
    envItems << QString("_preview_enabled=%1").arg(static_cast<int>(_filterContext.previewCheckBox));
    envItems << QString("_randomized=%1").arg(static_cast<int>(_filterContext.randomized));
  }
  // Initialized in case LayersExtentProxy::getExtent() leaves them untouched
  int maxWidth = 0;
  int maxHeight = 0;
  int preview_x0;
  int preview_y0;
  int preview_x1;
  int preview_y1;
  QSize previewSize;
  LayersExtentProxy::getExtent(_filterContext.inputOutputState.inputMode, maxWidth, maxHeight);
  if (_filterContext.previewFromFullImage) {
    preview_x0 = clampedToInt(rect.x * maxWidth);
    preview_y0 = clampedToInt(rect.y * maxHeight);
    preview_x1 = preview_x0 + std::min(maxWidth, clampedToInt(1 + std::ceil(maxWidth * rect.w))) - 1;
    preview_y1 = preview_y0 + std::min(maxHeight, clampedToInt(1 + std::ceil(maxHeight * rect.h))) - 1;
    previewSize = QSize(1 + preview_x1 - preview_x0, 1 + preview_y1 - preview_y0);
    if (_filterContext.zoomFactor < 1.0) {
      previewSize = QSize(clampedToInt(std::round(previewSize.width() * _filterContext.zoomFactor)), //
                          clampedToInt(std::round(previewSize.height() * _filterContext.zoomFactor)));
    }
  } else {
    if (_filterContext.zoomFactor < 1.0) {
      maxWidth = clampedToInt(std::round(maxWidth * _filterContext.zoomFactor));
      maxHeight = clampedToInt(std::round(maxHeight * _filterContext.zoomFactor));
    }
    preview_x0 = 0;
    preview_y0 = 0;
    preview_x1 = std::min(maxWidth, clampedToInt(1 + std::ceil(maxWidth * rect.w))) - 1;
    preview_y1 = std::min(maxHeight, clampedToInt(1 + std::ceil(maxHeight * rect.h))) - 1;
    previewSize = QSize(1 + preview_x1 - preview_x0, 1 + preview_y1 - preview_y0);
  }
  envItems << QString("_preview_x0=%1").arg(preview_x0);
  envItems << QString("_preview_y0=%1").arg(preview_y0);
  envItems << QString("_preview_x1=%1").arg(preview_x1);
  envItems << QString("_preview_y1=%1").arg(preview_y1);
  envItems << QString("_preview_width=%1").arg(previewSize.width());
  envItems << QString("_preview_height=%1").arg(previewSize.height());
  const QString env = envItems.join(QChar(' '));
  _completedExecutionTime.restart();
  if (_filterContext.requestType == FilterContext::RequestType::SynchronousPreview) {
    FilterSyncRunner runner(this, _filterContext.filterCommand, _filterContext.filterArguments, env);
    runner.swapImages(*_gmicImages);
    runner.setImageNames(imageNames);
    runner.setLogSuffix("preview");
    gmic_library::cimg::srand();
    _previewRandomSeed = gmic_library::cimg::_rand();
    _ongoingFilterExecutionTime.restart();
    runner.run();
    manageSynchonousRunner(runner);
    recordPreviewFilterExecutionDurationMS(clampedMilliseconds(_ongoingFilterExecutionTime.elapsed()));
  } else if ((_filterContext.requestType == FilterContext::RequestType::Preview) || //
             (_filterContext.requestType == FilterContext::RequestType::GUIDynamismRun)) {
    _filterThread = new FilterThread(this, _filterContext.filterCommand, _filterContext.filterArguments, env);
    _filterThread->swapImages(*_gmicImages);
    _filterThread->setImageNames(imageNames);
    _filterThread->setLogSuffix("preview");
    if (_filterContext.requestType == FilterContext::RequestType::Preview) {
      connect(_filterThread, &FilterThread::finished, this, &GmicProcessor::onPreviewThreadFinished, Qt::QueuedConnection);
    } else {
      connect(_filterThread, &FilterThread::finished, this, &GmicProcessor::onGUIDynamismThreadFinished, Qt::QueuedConnection);
    }
    gmic_library::cimg::srand();
    _previewRandomSeed = gmic_library::cimg::_rand();
    _ongoingFilterExecutionTime.restart();
    _filterThread->start();
  } else if (_filterContext.requestType == FilterContext::RequestType::FullImage) {
    _lastAppliedFilterHash = _filterContext.filterHash;
    _lastAppliedFilterPath = _filterContext.filterFullPath;
    _lastAppliedCommand = _filterContext.filterCommand;
    _lastAppliedCommandArguments = _filterContext.filterArguments;
    _lastAppliedCommandInOutState = _filterContext.inputOutputState;
    _filterThread = new FilterThread(this, _filterContext.filterCommand, _filterContext.filterArguments, env);
    _filterThread->swapImages(*_gmicImages);
    _filterThread->setImageNames(imageNames);
    _filterThread->setLogSuffix("apply");
    connect(_filterThread, &FilterThread::finished, this, &GmicProcessor::onApplyThreadFinished, Qt::QueuedConnection);
    gmic_library::cimg::srand(_previewRandomSeed);
    _filterThread->start();
  }
}

bool GmicProcessor::isProcessingFullImage() const
{
  return (_filterThread != nullptr) && (_filterContext.requestType == FilterContext::RequestType::FullImage);
}

bool GmicProcessor::isProcessing() const
{
  return _filterThread != nullptr;
}

bool GmicProcessor::isIdle() const
{
  return _filterThread == nullptr;
}

int GmicProcessor::duration() const
{
  if (_filterThread) {
    return _filterThread->duration();
  }
  return 0;
}

float GmicProcessor::progress() const
{
  if (_filterThread) {
    return _filterThread->progress();
  }
  return 0.0f;
}

bool GmicProcessor::hasPreviewFilterExecutionDurations() const
{
  return !_lastFilterPreviewExecutionDurations.empty();
}

int GmicProcessor::lastPreviewFilterExecutionDurationMS() const
{
  if (_lastFilterPreviewExecutionDurations.empty()) {
    return 0;
  }
  return _lastFilterPreviewExecutionDurations.back();
}

void GmicProcessor::resetLastPreviewFilterExecutionDurations()
{
  _lastFilterPreviewExecutionDurations.clear();
}

void GmicProcessor::recordPreviewFilterExecutionDurationMS(int duration)
{
  _lastFilterPreviewExecutionDurations.push_back(duration);
  while (_lastFilterPreviewExecutionDurations.size() >= static_cast<size_t>(KEYPOINTS_INTERACTIVE_AVERAGING_COUNT)) {
    _lastFilterPreviewExecutionDurations.pop_front();
  }
}

int GmicProcessor::averagePreviewFilterExecutionDuration() const
{
  if (_lastFilterPreviewExecutionDurations.empty()) {
    return 0;
  }
  double sum = 0.0;
  for (const int duration : _lastFilterPreviewExecutionDurations) {
    sum += duration;
  }
  return static_cast<int>(sum / static_cast<double>(_lastFilterPreviewExecutionDurations.size()));
}

int GmicProcessor::completedFullImageProcessingCount() const
{
  return _completeFullImageProcessingCount;
}

qint64 GmicProcessor::lastCompletedExecutionTime() const
{
  return _lastCompletedExecutionTime;
}

void GmicProcessor::cancel()
{
  abortCurrentFilterThread();
}

void GmicProcessor::detachAllUnfinishedAbortedThreads()
{
  for (FilterThread * thread : _unfinishedAbortedThreads) {
    thread->disconnect(this);
    thread->setParent(nullptr);
    // Nobody owns this thread anymore: let it delete itself once finished instead of leaking it.
    if (thread->isFinished()) {
      thread->deleteLater();
    } else {
      connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    }
  }
  _unfinishedAbortedThreads.clear();
}

bool GmicProcessor::hasUnfinishedAbortedThreads() const
{
  return !_unfinishedAbortedThreads.isEmpty();
}

const gmic_library::gmic_image<float> & GmicProcessor::previewImage() const
{
  return *_previewImage;
}

const QStringList & GmicProcessor::gmicStatus() const
{
  return _gmicStatus;
}

void GmicProcessor::saveSettings(QSettings & settings)
{
  const QString prefix = QString("LastExecution/host_%1/").arg(GmicQtHost::ApplicationShortname);
  if (_lastAppliedCommand.isEmpty()) {
    const QString empty;
    settings.setValue(prefix + "FilterHash", empty);
    settings.setValue(prefix + "FilterPath", empty);
    settings.setValue(prefix + "Command", empty);
    settings.setValue(prefix + "Arguments", empty);
    settings.setValue(prefix + "GmicStatusString", QString());
    settings.setValue(prefix + "InputMode", 0);
    settings.setValue(prefix + "OutputMode", 0);
  } else {
    settings.setValue(prefix + "FilterPath", _lastAppliedFilterPath);
    settings.setValue(prefix + "FilterHash", _lastAppliedFilterHash);
    settings.setValue(prefix + "Command", _lastAppliedCommand);
    settings.setValue(prefix + "Arguments", _lastAppliedCommandArguments);
    const QString status = flattenGmicParameterList(_lastAppliedCommandGmicStatus, _gmicStatusQuotedParameters);
    settings.setValue(prefix + "GmicStatusString", status);
    settings.setValue(prefix + "InputMode", static_cast<int>(_lastAppliedCommandInOutState.inputMode));
    settings.setValue(prefix + "OutputMode", static_cast<int>(_lastAppliedCommandInOutState.outputMode));
  }
}

void GmicProcessor::setGmicStatusQuotedParameters(const QVector<bool> & quotedParameters)
{
  _gmicStatusQuotedParameters = quotedParameters;
}

void GmicProcessor::onGUIDynamismThreadFinished()
{
  if (!_filterThread) {
    // A "finished" event posted before abortCurrentFilterThread() disconnected the thread
    // may still be delivered afterwards (disconnecting does not remove queued events).
    return;
  }
  if (_filterThread->isRunning()) {
    return;
  }
  if (_filterThread->failed()) {
    _gmicStatus.clear();
    _parametersVisibilityStates.clear();
    _gmicImages->assign();
    const QString message = _filterThread->errorMessage();
    releaseFilterThread();
    hideWaitingCursor();
    Logger::warning(QString("Failed to execute filter: %1").arg(message));
    return;
  }
  _gmicStatus = _filterThread->gmicStatus();
  _parametersVisibilityStates = _filterThread->parametersVisibilityStates();
  _gmicImages->assign();
  FilterGuiDynamismCache::setValue(_filterContext.filterHash, _gmicStatus.isEmpty() ? FilterGuiDynamism::Static : FilterGuiDynamism::Dynamic);
  PersistentMemory::move_from(_filterThread->persistentMemoryOutput());
  releaseFilterThread();
  hideWaitingCursor();
  emit guiDynamismRunDone();
}

void GmicProcessor::onPreviewThreadFinished()
{
  if (!_filterThread) {
    // A "finished" event posted before abortCurrentFilterThread() disconnected the thread
    // may still be delivered afterwards (disconnecting does not remove queued events).
    return;
  }
  if (_filterThread->isRunning()) {
    return;
  }
  _lastCompletedExecutionTime = _completedExecutionTime.elapsed();
  if (_filterThread->failed()) {
    _gmicStatus.clear();
    _parametersVisibilityStates.clear();
    _gmicImages->assign();
    const QString message = _filterThread->errorMessage();
    releaseFilterThread();
    hideWaitingCursor();
    emit previewCommandFailed(message);
    return;
  }
  _gmicStatus = _filterThread->gmicStatus();
  _parametersVisibilityStates = _filterThread->parametersVisibilityStates();
  _gmicImages->assign();
  FilterGuiDynamismCache::setValue(_filterContext.filterHash, _gmicStatus.isEmpty() ? FilterGuiDynamism::Static : FilterGuiDynamism::Dynamic);
  _filterThread->swapImages(*_gmicImages);
  PersistentMemory::move_from(_filterThread->persistentMemoryOutput());
  unsigned int badSpectrumIndex = 0;
  const bool correctSpectrums = checkImageSpectrumAtMost4(*_gmicImages, badSpectrumIndex);
  if (correctSpectrums) {
    for (unsigned int i = 0; i < _gmicImages->size(); ++i) {
      GmicQtHost::applyColorProfile((*_gmicImages)[i]);
    }
    buildPreviewImage(*_gmicImages, *_previewImage);
  }
  releaseFilterThread();
  hideWaitingCursor();
  if (correctSpectrums) {
    emit previewImageAvailable();
    recordPreviewFilterExecutionDurationMS(clampedMilliseconds(_ongoingFilterExecutionTime.elapsed()));
  } else {
    const QString message(tr("Image #%1 returned by filter has %2 channels (should be at most 4)"));
    emit previewCommandFailed(message.arg(badSpectrumIndex).arg((*_gmicImages)[badSpectrumIndex].spectrum()));
  }
}

void GmicProcessor::onApplyThreadFinished()
{
  if (!_filterThread) {
    // A "finished" event posted before abortCurrentFilterThread() disconnected the thread
    // may still be delivered afterwards (disconnecting does not remove queued events).
    return;
  }
  Q_ASSERT_X(!_filterThread->aborted(), Q_FUNC_INFO, "Aborted thread!");
  if (_filterThread->isRunning()) {
    return;
  }
  _lastCompletedExecutionTime = _completedExecutionTime.elapsed();
  _gmicStatus = _filterThread->gmicStatus();
  _parametersVisibilityStates = _filterThread->parametersVisibilityStates();
  hideWaitingCursor();

  if (_filterThread->failed()) {
    _lastAppliedFilterPath.clear();
    _lastAppliedCommand.clear();
    _lastAppliedCommandArguments.clear();
    const QString message = _filterThread->errorMessage();
    releaseFilterThread();
    emit fullImageProcessingFailed(message);
  } else {
    _filterThread->swapImages(*_gmicImages);
    PersistentMemory::move_from(_filterThread->persistentMemoryOutput());
    unsigned int badSpectrumIndex = 0;
    const bool correctSpectrums = checkImageSpectrumAtMost4(*_gmicImages, badSpectrumIndex);
    if (!correctSpectrums) {
      _lastAppliedFilterPath.clear();
      _lastAppliedCommand.clear();
      _lastAppliedCommandArguments.clear();
      releaseFilterThread();
      const QString message(tr("Image #%1 returned by filter has %2 channels\n(should be at most 4)"));
      emit fullImageProcessingFailed(message.arg(badSpectrumIndex).arg((*_gmicImages)[badSpectrumIndex].spectrum()));
    } else {
      if (GmicQtHost::ApplicationName.isEmpty()) {
        emit aboutToSendImagesToHost();
      }
      GmicQtHost::outputImages(*_gmicImages, _filterThread->imageNames(), _filterContext.inputOutputState.outputMode);
      _completeFullImageProcessingCount += 1;
      LayersExtentProxy::clear();
      CroppedActiveLayerProxy::clear();
      CroppedImageListProxy::clear();
      releaseFilterThread();
      _lastAppliedCommandGmicStatus = resolveUnchangedStatusValues(_gmicStatus, _filterContext.filterParameters); // TODO : save visibility states?
      emit fullImageProcessingDone();
    }
  }
}

void GmicProcessor::onAbortedThreadFinished()
{
  FilterThread * thread = qobject_cast<FilterThread *>(sender());
  if (!thread || !_unfinishedAbortedThreads.removeOne(thread)) {
    return;
  }
  thread->deleteLater();
  if (_unfinishedAbortedThreads.isEmpty()) {
    emit noMoreUnfinishedJobs();
  }
}

void GmicProcessor::showWaitingCursor()
{
  if (_filterThread) {
    OverrideCursor::set(Qt::WaitCursor);
  }
}

void GmicProcessor::hideWaitingCursor()
{
  _waitingCursorTimer.stop();
  OverrideCursor::setNormal();
}

void GmicProcessor::updateImageNames(gmic_list<char> & imageNames)
{
  int maxWidth = 0;
  int maxHeight = 0;
  LayersExtentProxy::getExtent(_filterContext.inputOutputState.inputMode, maxWidth, maxHeight);
  if ((maxWidth <= 0) || (maxHeight <= 0)) {
    return; // Avoids divisions by zero (and the conversion of infinite values to int below)
  }
  const double xFactor = _filterContext.positionStringCorrection.xFactor / static_cast<double>(maxWidth);
  const double yFactor = _filterContext.positionStringCorrection.yFactor / static_cast<double>(maxHeight);
  // Compiled only once (a QRegularExpression can safely be used concurrently through const methods)
  static const QRegularExpression re(QStringLiteral("pos\\((\\d*)([^0-9]*)(\\d*)\\)"));
  for (size_t i = 0; i < imageNames.size(); ++i) {
    gmic_image<char> & name = imageNames[i];
    if (!name.data() || !name.size()) {
      continue;
    }
    // Do not assume that the name is null-terminated
    const char * const nameEnd = static_cast<const char *>(std::memchr(name.data(), 0, name.size()));
    const int nameLength = nameEnd ? static_cast<int>(nameEnd - name.data()) : static_cast<int>(name.size());
    QString str = QString::fromUtf8(name.data(), nameLength);
    const QRegularExpressionMatch match = re.match(str);
    if (!match.hasMatch() || match.captured(1).isEmpty() || match.captured(3).isEmpty()) {
      continue;
    }
    bool xOk = false;
    bool yOk = false;
    const qlonglong xPos = match.captured(1).toLongLong(&xOk);
    const qlonglong yPos = match.captured(3).toLongLong(&yOk);
    if (!xOk || !yOk) {
      continue; // Out of range numbers
    }
    const int newXPos = clampedToInt(static_cast<double>(xPos) * xFactor);
    const int newYPos = clampedToInt(static_cast<double>(yPos) * yFactor);
    // Replace the matched range only (replacing the matched text would also alter any other identical occurrence)
    str.replace(match.capturedStart(0), match.capturedLength(0), QString("pos(%1%2%3)").arg(newXPos).arg(match.captured(2)).arg(newYPos));
    const QByteArray utf8 = str.toUtf8(); // Image names are UTF-8 encoded (see QString::fromUtf8() above)
    name.assign(static_cast<unsigned int>(utf8.size()) + 1);
    std::memcpy(name.data(), utf8.constData(), static_cast<size_t>(utf8.size()) + 1); // Includes the terminating null character
  }
}

void GmicProcessor::abortCurrentFilterThread()
{
  if (!_filterThread) {
    return;
  }
  _filterThread->disconnect(this);
  connect(_filterThread, &FilterThread::finished, this, &GmicProcessor::onAbortedThreadFinished);
  _unfinishedAbortedThreads.push_back(_filterThread);
  _filterThread->abortGmic();
  _filterThread = nullptr;
  _waitingCursorTimer.stop();
  OverrideCursor::setNormal();
}

void GmicProcessor::releaseFilterThread()
{
  if (_filterThread) {
    _filterThread->deleteLater();
    _filterThread = nullptr;
  }
}

void GmicProcessor::manageSynchonousRunner(FilterSyncRunner & runner)
{
  _lastCompletedExecutionTime = _completedExecutionTime.elapsed();
  if (runner.failed()) {
    _gmicStatus.clear();
    _gmicImages->assign();
    const QString message = runner.errorMessage();
    hideWaitingCursor();
    emit previewCommandFailed(message);
    return;
  }
  _gmicStatus = runner.gmicStatus();
  _parametersVisibilityStates = runner.parametersVisibilityStates();
  _gmicImages->assign();
  runner.swapImages(*_gmicImages);
  PersistentMemory::move_from(runner.persistentMemoryOutput());
  // Same check as in onPreviewThreadFinished(): otherwise the extra channels were silently dropped here
  unsigned int badSpectrumIndex = 0;
  if (!checkImageSpectrumAtMost4(*_gmicImages, badSpectrumIndex)) {
    const int spectrum = (*_gmicImages)[badSpectrumIndex].spectrum();
    hideWaitingCursor();
    const QString message(tr("Image #%1 returned by filter has %2 channels (should be at most 4)"));
    emit previewCommandFailed(message.arg(badSpectrumIndex).arg(spectrum));
    return;
  }
  for (unsigned int i = 0; i < _gmicImages->size(); ++i) {
    GmicQtHost::applyColorProfile((*_gmicImages)[i]);
  }
  buildPreviewImage(*_gmicImages, *_previewImage);
  hideWaitingCursor();
  emit previewImageAvailable();
}

const QList<int> & GmicProcessor::parametersVisibilityStates() const
{
  return _parametersVisibilityStates;
}

} // namespace GmicQt
