/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file ProgressInfoWidget.cpp
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
#include "Widgets/ProgressInfoWidget.h"
#include <QFile>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>
#include <limits>
#include "Common.h"
#include "GmicProcessor.h"
#include "IconLoader.h"
#include "Misc.h"
#include "ui_progressinfowidget.h"

#ifdef _IS_WINDOWS_
#include <windows.h>
#include <process.h>
#include <psapi.h>
#endif

namespace
{

#if defined(_IS_UNIX_)
/**
 * Looks for the "VmRSS:" entry of the content of /proc/self/status and reads the (unsigned)
 * number which follows. Works on a bounded buffer, and reports any failure.
 *
 * This replaces strstr() + sscanf(str + 7, "%llu", ...) where
 *  - sscanf() returns EOF (a non-zero value) when no number is found, so that an uninitialized
 *    variable was then used as a memory size;
 *  - "str + 7" could point past the end of the buffer if "VmRSS:" was its last characters.
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
#endif

#if defined(_IS_UNIX_) || defined(_IS_WINDOWS_)
/**
 * Memory currently used by the process, in bytes.
 * @return false if the information is not available.
 */
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
#else // _IS_WINDOWS_
  PROCESS_MEMORY_COUNTERS counters = {};
  counters.cb = sizeof(counters);
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
    return false;
  }
  bytes = static_cast<quint64>(counters.WorkingSetSize);
  return true;
#endif
}
#endif

} // namespace

namespace GmicQt
{

ProgressInfoWidget::ProgressInfoWidget(QWidget * parent) : QWidget(parent), ui(new Ui::ProgressInfoWidget), _gmicProcessor(nullptr)
{
  ui->setupUi(this);
  _mode = Mode::GmicProcessing;
  _canceled = false;
  _growing = true;
  setWindowTitle(tr("G'MIC-Qt Plug-in progression"));
  ui->progressBar->setRange(0, 100);
  ui->tbCancel->setIcon(IconLoader::load("cancel"));
  ui->tbCancel->setToolTip(tr("Abort"));
  connect(&_timer, &QTimer::timeout, this, &ProgressInfoWidget::onTimeOut);
  connect(ui->tbCancel, &QToolButton::clicked, this, &ProgressInfoWidget::cancel);
  if (!parent) {
    QRect position = frameGeometry();
    QList<QScreen *> screens = QGuiApplication::screens();
    if (!screens.isEmpty()) {
      position.moveCenter(screens.front()->geometry().center());
      move(position.topLeft());
    }
  }

  _showingTimer.setSingleShot(true);
  _showingTimer.setInterval(500);
  connect(&_showingTimer, &QTimer::timeout, this, &ProgressInfoWidget::onTimeOut);
  connect(&_showingTimer, &QTimer::timeout, &_timer, QOverload<>::of(&QTimer::start));
  connect(&_showingTimer, &QTimer::timeout, this, &ProgressInfoWidget::show);
}

ProgressInfoWidget::~ProgressInfoWidget()
{
  delete ui;
}

ProgressInfoWidget::Mode ProgressInfoWidget::mode() const
{
  return _mode;
}

bool ProgressInfoWidget::hasBeenCanceled() const
{
  return _canceled;
}

void ProgressInfoWidget::setGmicProcessor(const GmicProcessor * processor)
{
  _gmicProcessor = processor;
}

void ProgressInfoWidget::onTimeOut()
{
  if (_mode == Mode::GmicProcessing) {
    updateThreadInformation();
  } else if (_mode == Mode::FiltersUpdate) {
    updateFilterUpdateProgression();
  }
}

void ProgressInfoWidget::cancel()
{
  _canceled = true;
  emit canceled();
}

void ProgressInfoWidget::stopAnimationAndHide()
{
  _timer.stop();
  _showingTimer.stop();
  hide();
}

void ProgressInfoWidget::startFilterThreadAnimationAndShow()
{
  layout()->removeWidget(ui->tbCancel);
  layout()->removeWidget(ui->progressBar);
  layout()->removeWidget(ui->label);
  layout()->addWidget(ui->progressBar);
  layout()->addWidget(ui->label);
  ui->tbCancel->hide();

  ui->label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  ui->label->setAlignment(Qt::AlignRight);
#if defined(_IS_UNIX_) || defined(_IS_WINDOWS_)
  QString largestText(tr("[Processing 88:00:00.888 | 888.9 GiB]"));
#else
  QString largestText(tr("[Processing 88:00:00.888]"));
#endif
  QFontMetrics fm(ui->label->font());
#if QT_VERSION_GTE(5, 11, 0)
  ui->label->setMinimumWidth(fm.horizontalAdvance(largestText));
#else
  ui->label->setMinimumWidth(fm.width(largestText));
#endif

  _canceled = false;
  _mode = Mode::GmicProcessing;
  ui->progressBar->setRange(0, 100);
  ui->progressBar->setValue(0);
  ui->progressBar->setInvertedAppearance(false);
  onTimeOut();
  _timer.setInterval(250);
  _timer.start();
  show();
}

void ProgressInfoWidget::startFiltersUpdateAnimationAndShow()
{

  layout()->removeWidget(ui->tbCancel);
  layout()->removeWidget(ui->progressBar);
  layout()->removeWidget(ui->label);
  layout()->addWidget(ui->label);
  layout()->addWidget(ui->tbCancel);
  layout()->addWidget(ui->progressBar);

  _mode = Mode::FiltersUpdate;
  _canceled = false;
  // ui->progressBar->setRange(0, 0);
  ui->progressBar->setValue(AnimationStep);
  ui->progressBar->setTextVisible(false);
  ui->progressBar->setInvertedAppearance(false);
  ui->label->setText(tr("Updating filters..."));
  ui->label->setMinimumWidth(0);
  ui->label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  ui->label->setAlignment(Qt::AlignLeft);
  _timer.setInterval(75);

  _growing = true;
  ui->tbCancel->setVisible(true);

  _showingTimer.start();
  // Connected to :
  // 1. onTimeOut();
  // 2. _timer.start();
  // 3. show();
}

void ProgressInfoWidget::showBusyIndicator()
{
  ui->progressBar->setRange(0, 0);
}

void ProgressInfoWidget::updateThreadInformation()
{
  if (!_gmicProcessor) {
    // The timer may fire before setGmicProcessor() has been called.
    return;
  }
  int ms = _gmicProcessor->duration();
  float progress = _gmicProcessor->progress();
  if (progress >= 0) {
    ui->progressBar->setInvertedAppearance(false);
    ui->progressBar->setTextVisible(true);
    // Bounded before the conversion: casting a float which is out of the range of int is undefined.
    ui->progressBar->setValue(static_cast<int>(std::min(progress, 100.0f)));
  } else {
    ui->progressBar->setTextVisible(false);
    int value = ui->progressBar->value();
    value += 20;
    if (value > 100) {
      ui->progressBar->setValue(value - 100);
      ui->progressBar->setInvertedAppearance(!ui->progressBar->invertedAppearance());
    } else {
      ui->progressBar->setValue(value);
    }
  }
  QString durationStr = readableDuration(ms);
#if defined(_IS_UNIX_) || defined(_IS_WINDOWS_)
  quint64 memoryBytes = 0;
  const QString memoryStr = processMemoryUsage(memoryBytes) ? readableSize(memoryBytes) : QString("? KiB");
  ui->label->setText(QString(tr("[Processing %1 | %2]")).arg(durationStr).arg(memoryStr));
#else
  ui->label->setText(QString(tr("[Processing %1]")).arg(durationStr));
#endif
}

void ProgressInfoWidget::updateFilterUpdateProgression()
{
  int value = ui->progressBar->value();
  if (_growing) {
    value += AnimationStep;
    if (value >= 100) {
      value = 100 - AnimationStep;
      ui->progressBar->setInvertedAppearance(!ui->progressBar->invertedAppearance());
      ui->progressBar->setValue(value);
      _growing = false;
    } else {
      ui->progressBar->setValue(value);
    }
  } else {
    value -= AnimationStep;
    if (value <= 0) {
      value = AnimationStep;
      ui->progressBar->setValue(value);
      _growing = true;
    } else {
      ui->progressBar->setValue(value);
    }
  }
}

} // namespace GmicQt
