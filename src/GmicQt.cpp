/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file GmicQt.cpp
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
#include "GmicQt.h"
#include <QApplication>
#include <QDebug>
#include <QImage>
#include <QList>
#include <QLocale>
#include <QSettings>
#include <QString>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include "Common.h"
#include "Globals.h"
#include "HeadlessProcessor.h"
#include "LanguageSettings.h"
#include "Logger.h"
#include "MainWindow.h"
#include "Misc.h"
#include "Settings.h"
#include "Widgets/InOutPanel.h"
#include "Widgets/ProgressInfoWindow.h"
#include "gmic.h"
#ifdef _IS_MACOS_
#include <libgen.h>
#include <mach-o/dyld.h>
#include <stdlib.h>
#endif

namespace
{
void configureApplication();
void disableModes(const std::list<GmicQt::InputMode> & disabledInputModes, //
                  const std::list<GmicQt::OutputMode> & disabledOutputModes);

inline bool archIsLittleEndian()
{
  const int x = 1;
  return (*reinterpret_cast<const unsigned char *>(&x));
}

// Converting a NaN, or a value which is out of range, from float to an integer type is
// undefined behavior: the negated comparison below also maps NaN to 0.
inline unsigned char float2uchar_bounded(const float & in)
{
  return !(in > 0.0f) ? 0 : ((in >= 255.0f) ? 255 : static_cast<unsigned char>(in));
}

// Values read from the settings may be corrupted or come from another version of the plug-in:
// never cast them blindly to an enumeration.
GmicQt::InputMode inputModeFromInt(const int value, const GmicQt::InputMode fallback)
{
  switch (value) {
  case static_cast<int>(GmicQt::InputMode::NoInput):
  case static_cast<int>(GmicQt::InputMode::Active):
  case static_cast<int>(GmicQt::InputMode::All):
  case static_cast<int>(GmicQt::InputMode::ActiveAndBelow):
  case static_cast<int>(GmicQt::InputMode::ActiveAndAbove):
  case static_cast<int>(GmicQt::InputMode::AllVisible):
  case static_cast<int>(GmicQt::InputMode::AllInvisible):
  case static_cast<int>(GmicQt::InputMode::AllVisiblesDesc_DEPRECATED):
  case static_cast<int>(GmicQt::InputMode::AllInvisiblesDesc_DEPRECATED):
  case static_cast<int>(GmicQt::InputMode::AllDesc_DEPRECATED):
  case static_cast<int>(GmicQt::InputMode::Unspecified):
    return static_cast<GmicQt::InputMode>(value);
  default:
    return fallback;
  }
}

GmicQt::OutputMode outputModeFromInt(const int value, const GmicQt::OutputMode fallback)
{
  switch (value) {
  case static_cast<int>(GmicQt::OutputMode::InPlace):
  case static_cast<int>(GmicQt::OutputMode::NewLayers):
  case static_cast<int>(GmicQt::OutputMode::NewActiveLayers):
  case static_cast<int>(GmicQt::OutputMode::NewImage):
  case static_cast<int>(GmicQt::OutputMode::Unspecified):
    return static_cast<GmicQt::OutputMode>(value);
  default:
    return fallback;
  }
}

// Pixel value of the checkerboard (96 or 160) displayed behind transparent areas of a preview.
inline unsigned int checkerboardValue(const int x, const int y)
{
  return 96 + (((x ^ y) & 8) << 3);
}

template <typename T> //
inline unsigned int alphaValue(const T alpha)
{
  // Bounded to [0,255]: 255 - alpha used to wrap around for an alpha value above 255.
  return float2uchar_bounded(static_cast<float>(alpha));
}

// Blends the first `colorChannels` channels of img over a checkerboard, according to the
// alpha channel `alphaChannel` (only the first slice is processed, as it used to be).
// This replaces five copies of the same loop.
template <typename T> //
void blendOnCheckerboard(gmic_library::gmic_image<T> & img, const unsigned int alphaChannel, const unsigned int colorChannels)
{
  const int w = img.width();
  const int h = img.height();
  for (unsigned int c = 0; c < colorChannels; ++c) {
    T * color = img.data(0, 0, 0, c);
    const T * alpha = img.data(0, 0, 0, alphaChannel);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const unsigned int a = alphaValue(*alpha++);
        *color = static_cast<T>((a * static_cast<unsigned int>(*color) + (255 - a) * checkerboardValue(x, y)) >> 8);
        ++color;
      }
    }
  }
}

template <typename T> //
struct AverageAccumulator {
  typedef typename std::conditional<std::is_floating_point<T>::value, T, long long>::type type;
};

// Stores (R + G + B) / 3 in the first channel.
// The previous implementation, (c0 += c1 += c2) /= 3, accumulated in the pixel type itself:
// it wrapped around with 8-bit pixels (e.g. 200,100,100 gave 48 instead of 133).
// It also modified the second channel as a side effect.
template <typename T> //
void averageRgbIntoFirstChannel(gmic_library::gmic_image<T> & img)
{
  typedef typename AverageAccumulator<T>::type Accumulator;
  T * r = img.data(0, 0, 0, 0);
  const T * g = img.data(0, 0, 0, 1);
  const T * b = img.data(0, 0, 0, 2);
  const size_t count = static_cast<size_t>(img.width()) * static_cast<size_t>(img.height()) * static_cast<size_t>(img.depth());
  for (size_t i = 0; i < count; ++i) {
    r[i] = static_cast<T>((static_cast<Accumulator>(r[i]) + (static_cast<Accumulator>(g[i]) + static_cast<Accumulator>(b[i]))) / 3);
  }
}

} // namespace

namespace GmicQt
{
InputMode DefaultInputMode = InputMode::Active;
OutputMode DefaultOutputMode = OutputMode::InPlace;
const OutputMessageMode DefaultOutputMessageMode = OutputMessageMode::Quiet;
const int GmicVersion = gmic_version;

const QString & gmicVersionString()
{
  static QString value = QString("%1.%2.%3").arg(gmic_version / 100).arg((gmic_version / 10) % 10).arg(gmic_version % 10);
  return value;
}

RunParameters lastAppliedFilterRunParameters(ReturnedRunParametersFlag flag)
{
  configureApplication();
  RunParameters parameters;
  QSettings settings;
  const QString keyPrefix = QString("LastExecution/host_%1/").arg(GmicQtHost::ApplicationShortname);
  const QString path = settings.value(keyPrefix + "FilterPath").toString();
  parameters.filterPath = path.toStdString();
  QString args = settings.value(keyPrefix + "Arguments").toString();
  if (flag == ReturnedRunParametersFlag::AfterFilterExecution) {
    QString lastAppliedCommandGmicStatus = settings.value(keyPrefix + "GmicStatusString").toString();
    if (!lastAppliedCommandGmicStatus.isEmpty()) {
      args = lastAppliedCommandGmicStatus;
    }
  }
  QString command = settings.value(keyPrefix + "Command").toString();
  appendWithSpace(command, args);
  parameters.command = command.toStdString();
  parameters.inputMode = inputModeFromInt(settings.value(keyPrefix + "InputMode", (int)InputMode::Active).toInt(), InputMode::Active);
  parameters.outputMode = outputModeFromInt(settings.value(keyPrefix + "OutputMode", (int)OutputMode::InPlace).toInt(), OutputMode::InPlace);
  return parameters;
}

int run(UserInterfaceMode interfaceMode,                   //
        RunParameters parameters,                          //
        const std::list<InputMode> & disabledInputModes,   //
        const std::list<OutputMode> & disabledOutputModes, //
        bool * dialogWasAccepted)
{
  // The output parameter has a defined value on every code path.
  setValueIfNotNullPointer(dialogWasAccepted, false);

  int dummy_argc = 1;
  char dummy_app_name[] = GMIC_QT_APPLICATION_NAME;

#ifdef _IS_WINDOWS_
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif

  // Owns the buffer returned by realpath(), which must outlive the QCoreApplication instance
  // (Qt keeps a pointer to argv). It was never freed.
  std::unique_ptr<char, decltype(&std::free)> executableFullPath(nullptr, &std::free);
  char * fullexname = nullptr;
#ifdef _IS_MACOS_
  {
    char exname[2048] = {0};
    // get the path where the executable is stored
    uint32_t size = sizeof(exname);
    if (_NSGetExecutablePath(exname, &size) == 0) {
      printf("Executable path is [%s]\n", exname);
      executableFullPath.reset(realpath(exname, nullptr));
      if (executableFullPath) {
        fullexname = executableFullPath.get();
        printf("Full executable name is [%s]\n", fullexname);
        // dirname() may modify its argument and/or return a pointer to static storage:
        // work on a copy, so that fullexname (used as argv[0]) is left untouched.
        std::vector<char> pathCopy(fullexname, fullexname + std::strlen(fullexname) + 1);
        const char * directory = dirname(pathCopy.data());
        if (directory) {
          printf("Full executable path is [%s]\n", directory);
          // std::string instead of a fixed 2048 bytes buffer: the former strncat() calls bounded the
          // number of appended characters, not the buffer size, and could overflow it.
          std::string pluginPath = std::string(directory) + "/GMIC/plugins/";
          const char * envPath = getenv("QT_PLUGIN_PATH");
          if (envPath && *envPath) {
            // No empty (i.e. "current folder") entry is appended when QT_PLUGIN_PATH is unset.
            pluginPath += ':';
            pluginPath += envPath;
          }
          printf("Plugins path is [%s]\n", pluginPath.c_str());
          setenv("QT_PLUGIN_PATH", pluginPath.c_str(), 1);
        }
      } else {
        fprintf(stderr, "Could not resolve the executable path [%s]\n", exname);
      }
    } else {
      fprintf(stderr, "Buffer too small; need size %u\n", size);
    }
    setenv("QT_DEBUG_PLUGINS", "1", 1);
  }
#endif
  if (!fullexname) {
    fullexname = dummy_app_name;
  }
  char * dummy_argv[1] = {fullexname};

  disableModes(disabledInputModes, disabledOutputModes);
  if (interfaceMode == UserInterfaceMode::Silent) {
    configureApplication();
    QCoreApplication app(dummy_argc, dummy_argv);
    Settings::load(interfaceMode);
    Logger::setMode(Settings::outputMessageMode());
    HeadlessProcessor processor(&app);
    if (!processor.setPluginParameters(parameters)) {
      Logger::error(processor.error());
      setValueIfNotNullPointer(dialogWasAccepted, false);
      return 1;
    }
    QTimer::singleShot(0, &processor, &HeadlessProcessor::startProcessing);
    int status = QCoreApplication::exec();
    setValueIfNotNullPointer(dialogWasAccepted, processor.processingCompletedProperly());
    return status;
  } else if (interfaceMode == UserInterfaceMode::ProgressDialog) {
    configureApplication();
    QApplication app(dummy_argc, dummy_argv);
    QApplication::setWindowIcon(QIcon(":resources/gmic_hat.png"));
    Settings::load(interfaceMode);
    Logger::setMode(Settings::outputMessageMode());
    LanguageSettings::installTranslators();
    HeadlessProcessor processor(&app);
    if (!processor.setPluginParameters(parameters)) {
      Logger::error(processor.error());
      setValueIfNotNullPointer(dialogWasAccepted, false);
      return 1;
    }
    ProgressInfoWindow progressWindow(&processor);
    unused(progressWindow);
    processor.startProcessing();
    int status = QApplication::exec();
    setValueIfNotNullPointer(dialogWasAccepted, processor.processingCompletedProperly());
    return status;
  } else if (interfaceMode == UserInterfaceMode::Full) {
    configureApplication();
    QApplication app(dummy_argc, dummy_argv);
    QApplication::setWindowIcon(QIcon(":resources/gmic_hat.png"));
    Settings::load(interfaceMode);
    LanguageSettings::installTranslators();
    MainWindow mainWindow;
    mainWindow.setPluginParameters(parameters);
    if (QSettings().value("Config/MainWindowMaximized", false).toBool()) {
      mainWindow.showMaximized();
    } else {
      mainWindow.show();
    }
    int status = QApplication::exec();
    setValueIfNotNullPointer(dialogWasAccepted, mainWindow.isAccepted());
    return status;
  }
  return 0;
}

std::string RunParameters::filterName() const
{
  const auto position = filterPath.rfind('/');
  if (position == std::string::npos) {
    return filterPath;
  }
  return filterPath.substr(position + 1);
}

template <typename T> //
void calibrateImage(gmic_library::gmic_image<T> & img, const int spectrum, const bool isPreview)
{
  if (!img || !spectrum) {
    return;
  }
  switch (spectrum) {
  case 1: // To GRAY
    switch (img.spectrum()) {
    case 1: // from GRAY
      break;
    case 2: // from GRAYA
      if (isPreview) {
        blendOnCheckerboard(img, 1, 1);
      }
      img.channel(0);
      break;
    case 3: // from RGB
      averageRgbIntoFirstChannel(img);
      img.channel(0);
      break;
    case 4: // from RGBA
      averageRgbIntoFirstChannel(img);
      if (isPreview) {
        blendOnCheckerboard(img, 3, 1);
      }
      img.channel(0);
      break;
    default: // from multi-channel (>4)
      img.channel(0);
    }
    break;

  case 2: // To GRAYA
    switch (img.spectrum()) {
    case 1: // from GRAY
      img.resize(-100, -100, 1, 2, 0).get_shared_channel(1).fill(255);
      break;
    case 2: // from GRAYA
      break;
    case 3: // from RGB
      averageRgbIntoFirstChannel(img);
      img.channels(0, 1).get_shared_channel(1).fill(255);
      break;
    case 4: // from RGBA
      averageRgbIntoFirstChannel(img);
      img.get_shared_channel(1) = img.get_shared_channel(3);
      img.channels(0, 1);
      break;
    default: // from multi-channel (>4)
      img.channels(0, 1);
    }
    break;

  case 3: // to RGB
    switch (img.spectrum()) {
    case 1: // from GRAY
      img.resize(-100, -100, 1, 3);
      break;
    case 2: // from GRAYA
      if (isPreview) {
        blendOnCheckerboard(img, 1, 1);
      }
      img.channel(0).resize(-100, -100, 1, 3);
      break;
    case 3: // from RGB
      break;
    case 4: // from RGBA
      if (isPreview) {
        blendOnCheckerboard(img, 3, 3);
      }
      img.channels(0, 2);
      break;
    default: // from multi-channel (>4)
      img.channels(0, 2);
    }
    break;

  case 4: // to RGBA
    switch (img.spectrum()) {
    case 1: // from GRAY
      img.resize(-100, -100, 1, 4).get_shared_channel(3).fill(255);
      break;
    case 2: // from GRAYA
      img.resize(-100, -100, 1, 4, 0);
      img.get_shared_channel(3) = img.get_shared_channel(1);
      img.get_shared_channel(1) = img.get_shared_channel(0);
      img.get_shared_channel(2) = img.get_shared_channel(0);
      break;
    case 3: // from RGB
      img.resize(-100, -100, 1, 4, 0).get_shared_channel(3).fill(255);
      break;
    case 4: // from RGBA
      break;
    default: // from multi-channel (>4)
      img.channels(0, 3);
    }
    break;
  }
}

template void calibrateImage(gmic_library::gmic_image<gmic_pixel_type> & img, const int spectrum, const bool is_preview);
template void calibrateImage(gmic_library::gmic_image<unsigned char> & img, const int spectrum, const bool is_preview);

void convertGmicImageToQImage(const gmic_library::gmic_image<float> & in, QImage & out)
{
  if (in.is_empty() || (in.width() <= 0) || (in.height() <= 0) || (in.spectrum() <= 0)) {
    out = QImage();
    return;
  }

  // The destination image is created directly with the right format. It used to be created as
  // RGB888 then converted (one more allocation and one more full copy, for every preview).
  QImage::Format format;
  if ((in.spectrum() >= 4) || (in.spectrum() == 2)) {
    format = QImage::Format_ARGB32;
  } else if (in.spectrum() == 3) {
    format = QImage::Format_RGB888;
  } else {
// Format_Grayscale8 was added in Qt 5.5.
#if QT_VERSION_GTE(5, 5, 0)
    format = QImage::Format_Grayscale8;
#else
    format = QImage::Format_RGB888;
#endif
  }
  out = QImage(in.width(), in.height(), format);
  if (out.isNull()) {
    // Allocation failure (huge image): scanLine() would return a null pointer.
    Logger::error(QString("convertGmicImageToQImage(): cannot allocate a %1x%2 image").arg(in.width()).arg(in.height()));
    return;
  }

  if (in.spectrum() >= 4) {
    const float * srcR = in.data(0, 0, 0, 0);
    const float * srcG = in.data(0, 0, 0, 1);
    const float * srcB = in.data(0, 0, 0, 2);
    const float * srcA = in.data(0, 0, 0, 3);
    int height = out.height();
    if (archIsLittleEndian()) {
      for (int y = 0; y < height; ++y) {
        int n = in.width();
        unsigned char * dst = out.scanLine(y);
        while (n--) {
          dst[0] = float2uchar_bounded(*srcB++);
          dst[1] = float2uchar_bounded(*srcG++);
          dst[2] = float2uchar_bounded(*srcR++);
          dst[3] = float2uchar_bounded(*srcA++);
          dst += 4;
        }
      }
    } else {
      for (int y = 0; y < height; ++y) {
        int n = in.width();
        unsigned char * dst = out.scanLine(y);
        while (n--) {
          dst[0] = float2uchar_bounded(*srcA++);
          dst[1] = float2uchar_bounded(*srcR++);
          dst[2] = float2uchar_bounded(*srcG++);
          dst[3] = float2uchar_bounded(*srcB++);
          dst += 4;
        }
      }
    }
  } else if (in.spectrum() == 3) {
    const float * srcR = in.data(0, 0, 0, 0);
    const float * srcG = in.data(0, 0, 0, 1);
    const float * srcB = in.data(0, 0, 0, 2);
    int height = out.height();
    for (int y = 0; y < height; ++y) {
      int n = in.width();
      unsigned char * dst = out.scanLine(y);
      while (n--) {
        dst[0] = float2uchar_bounded(*srcR++);
        dst[1] = float2uchar_bounded(*srcG++);
        dst[2] = float2uchar_bounded(*srcB++);
        dst += 3;
      }
    }
  } else if (in.spectrum() == 2) {
    //
    // Gray + Alpha
    //
    const float * src = in.data(0, 0, 0, 0);
    const float * srcA = in.data(0, 0, 0, 1);
    int height = out.height();
    if (archIsLittleEndian()) {
      for (int y = 0; y < height; ++y) {
        int n = in.width();
        unsigned char * dst = out.scanLine(y);
        while (n--) {
          dst[2] = dst[1] = dst[0] = float2uchar_bounded(*src++);
          dst[3] = float2uchar_bounded(*srcA++);
          dst += 4;
        }
      }
    } else {
      for (int y = 0; y < height; ++y) {
        int n = in.width();
        unsigned char * dst = out.scanLine(y);
        while (n--) {
          dst[1] = dst[2] = dst[3] = float2uchar_bounded(*src++);
          dst[0] = float2uchar_bounded(*srcA++);
          dst += 4;
        }
      }
    }
  } else {
    //
    // 8-bits Gray levels
    //
    const float * src = in.data(0, 0, 0, 0);
    int height = out.height();
    for (int y = 0; y < height; ++y) {
      int n = in.width();
      unsigned char * dst = out.scanLine(y);
#if QT_VERSION_GTE(5, 5, 0)
      while (n--) {
        // Bounded conversion (a plain static_cast is undefined for NaN or out of range values).
        *dst++ = float2uchar_bounded(*src++);
      }
#else
      while (n--) {
        dst[0] = float2uchar_bounded(*src);
        dst[1] = float2uchar_bounded(*src);
        dst[2] = float2uchar_bounded(*src);
        ++src;
        dst += 3;
      }
#endif
    }
  }
}

void convertQImageToGmicImage(const QImage & in, gmic_library::gmic_image<float> & out)
{
  if (in.isNull()) {
    out.assign();
    return;
  }

  // Only ARGB32 and RGB888 are handled below. With any other format, this function used to
  // do nothing in release builds (the check was a Q_ASSERT_X), leaving `out` unchanged.
  QImage converted;
  const QImage * source = &in;
  if ((in.format() != QImage::Format_ARGB32) && (in.format() != QImage::Format_RGB888)) {
    converted = in.convertToFormat(in.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB888);
    if (converted.isNull()) {
      out.assign();
      return;
    }
    source = &converted;
  }
  const QImage & image = *source;

  if (image.format() == QImage::Format_ARGB32) {
    const int w = image.width();
    const int h = image.height();
    out.assign(w, h, 1, 4);
    float * dstR = out.data(0, 0, 0, 0);
    float * dstG = out.data(0, 0, 0, 1);
    float * dstB = out.data(0, 0, 0, 2);
    float * dstA = out.data(0, 0, 0, 3);
    if (archIsLittleEndian()) {
      for (int y = 0; y < h; ++y) {
        const unsigned char * src = image.constScanLine(y);
        int n = w;
        while (n--) {
          *dstB++ = static_cast<float>(src[0]);
          *dstG++ = static_cast<float>(src[1]);
          *dstR++ = static_cast<float>(src[2]);
          *dstA++ = static_cast<float>(src[3]);
          src += 4;
        }
      }
    } else {
      for (int y = 0; y < h; ++y) {
        const unsigned char * src = image.constScanLine(y);
        int n = w;
        while (n--) {
          *dstA++ = static_cast<float>(src[0]);
          *dstR++ = static_cast<float>(src[1]);
          *dstG++ = static_cast<float>(src[2]);
          *dstB++ = static_cast<float>(src[3]);
          src += 4;
        }
      }
    }
    return;
  }

  // Format_RGB888
  const int w = image.width();
  const int h = image.height();
  out.assign(w, h, 1, 3);
  float * dstR = out.data(0, 0, 0, 0);
  float * dstG = out.data(0, 0, 0, 1);
  float * dstB = out.data(0, 0, 0, 2);
  for (int y = 0; y < h; ++y) {
    const unsigned char * src = image.constScanLine(y);
    int n = w;
    while (n--) {
      *dstR++ = static_cast<float>(src[0]);
      *dstG++ = static_cast<float>(src[1]);
      *dstB++ = static_cast<float>(src[2]);
      src += 3;
    }
  }
}

} // namespace GmicQt

namespace
{

void configureApplication()
{
  QCoreApplication::setOrganizationName(GMIC_QT_ORGANISATION_NAME);
  QCoreApplication::setOrganizationDomain(GMIC_QT_ORGANISATION_DOMAIN);
  QCoreApplication::setApplicationName(GMIC_QT_APPLICATION_NAME);
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
#if !QT_VERSION_GTE(6, 0, 0)
  if (QSettings().value(HIGHDPI_KEY, false).toBool()) {
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  }
#endif
}

void disableModes(const std::list<GmicQt::InputMode> & disabledInputModes, //
                  const std::list<GmicQt::OutputMode> & disabledOutputModes)
{
  for (const GmicQt::InputMode & mode : disabledInputModes) {
    GmicQt::InOutPanel::disableInputMode(mode);
  }
  for (const GmicQt::OutputMode & mode : disabledOutputModes) {
    GmicQt::InOutPanel::disableOutputMode(mode);
  }
}

} // namespace
