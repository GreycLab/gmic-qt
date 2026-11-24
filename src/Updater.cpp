/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file Updater.cpp
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
#include "Updater.h"
#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>
#include "Common.h"
#include "GmicStdlib.h"
#include "Logger.h"
#include "Misc.h"
#include "Settings.h"
#include "Utils.h"
#include "gmic.h"

namespace
{
// Maximum size of a filters source file (downloaded, or read from the disk), possibly CImg-compressed.
// The official definitions are a few megabytes at most: anything much larger is refused.
const qint64 MaxSourceFileSize = 64ll << 20;

// Maximum size of a source file once decompressed.
const std::size_t MaxDecompressedSize = static_cast<std::size_t>(256) << 20;

// Maximum size of the full filters library (concatenation of all the sources). It must also stay
// far below the size limit of a QByteArray.
const qint64 MaxFullStdlibSize = 256ll << 20;

// Upper bound for the download timeout (the value is multiplied by 1000 to get milliseconds).
const int MaxDownloadTimeoutSeconds = 24 * 3600;

// Maximum number of bytes of a failed reply that are written to the log.
const int MaxLoggedReplyBytes = 4096;

// Dynamic properties used to tell why a reply was aborted.
const char * const TooLargeProperty = "gmic_qt_download_too_large";
const char * const TimedOutProperty = "gmic_qt_download_timed_out";
} // namespace

namespace GmicQt
{
std::unique_ptr<Updater> Updater::_instance = std::unique_ptr<Updater>(nullptr);
OutputMessageMode Updater::_outputMessageMode = OutputMessageMode::Quiet;

const char * Updater::OfficialFilterSourceURL = "https://gmic.eu/update" GMIC_QT_XSTRINGIFY(gmic_version) ".gmic";

Updater::Updater(QObject * parent) : QObject(parent)
{
  _networkAccessManager = nullptr;
  _someNetworkUpdatesAchieved = false;
  // A single timer is shared by all the updates, so that the timeout of an update which is already
  // over cannot abort the downloads of a later one.
  _timeoutTimer = new QTimer(this);
  _timeoutTimer->setSingleShot(true);
  connect(_timeoutTimer, &QTimer::timeout, this, &Updater::cancelAllPendingDownloads);
}

bool Updater::isCImgCompressed(const QByteArray & data)
{
  return data.startsWith("1 uint8 ");
}

Updater * Updater::getInstance()
{
  if (!_instance) {
    _instance = std::unique_ptr<Updater>(new Updater(nullptr));
  }
  return _instance.get();
}

Updater::~Updater() = default;

void Updater::startUpdate(int ageLimit, int timeout, bool useNetwork)
{
  TIMING;
  if (!_pendingReplies.isEmpty()) {
    // Starting over would lose track of the pending replies. The updateIsDone() signal
    // will be emitted when the update in progress is over.
    Logger::warning("An update is already in progress: new request ignored");
    return;
  }
  QStringList sources = GmicStdLib::substituteSourceVariables(Settings::filterSources());
  prependOfficialSourceIfRelevant(sources);
  SHOW(sources);
  _errorMessages.clear();
  if (_networkAccessManager) { // Left over from a previous update
    _networkAccessManager->deleteLater();
  }
  _networkAccessManager = new QNetworkAccessManager(this);
  connect(_networkAccessManager, &QNetworkAccessManager::finished, this, &Updater::onNetworkReplyFinished);
  _someNetworkUpdatesAchieved = false;
  if (useNetwork) {
    const QDateTime limit = QDateTime::currentDateTime().addSecs(-3600ll * std::max(0, ageLimit));
    for (const QString & str : sources) {
      if (str.startsWith("http://") || str.startsWith("https://")) {
        const QString filename = localFilename(str);
        if (filename.isEmpty()) {
          _errorMessages << QString(tr("Error downloading %1 (cannot determine a local file name)")).arg(str);
          continue;
        }
        QFileInfo info(filename);
        if (!info.exists() || (info.lastModified() < limit)) {
          TRACE << "Downloading" << str << "to" << filename;
          QUrl url(str);
          QNetworkRequest request(url);
          request.setHeader(QNetworkRequest::UserAgentHeader, pluginFullName());
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
          // Qt 5 does not follow redirections by default (Qt 6 does): a redirected source ended up as
          // an "empty file" error. Redirections from https to http are still refused by this policy.
          request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#endif
          // PRIVACY NOTICE (to be displayed in one of the "About" filters of the plugin
          //
          // PRIVACY NOTICE
          // This plugin may download up-to-date filter definitions from the gmic.eu server.
          // It is the case when first launched after a fresh installation, and periodically
          // with a frequency which can be set in the settings dialog.
          // The user should be aware that the following information may be retrieved
          // from the server logs: IP address of the client; date and time of the request;
          // as well as a short string, supplied through the HTTP protocol "User Agent" header
          // field, which describes the full plugin version as shown in the window title
          // (e.g. "G'MIC-Qt for GIMP 2.8 - Linux 64 bits - 2.2.1_pre#180301").
          //
          // Note that this information may solely be used for purely anonymous
          // statistical purposes.
          QNetworkReply * reply = _networkAccessManager->get(request);
          // Do not let a (possibly user-supplied) source fill the memory: abort as soon as it is too large.
          connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64 total) {
            if (((received > MaxSourceFileSize) || (total > MaxSourceFileSize)) && !reply->property(TooLargeProperty).toBool()) {
              reply->setProperty(TooLargeProperty, true);
              reply->abort();
            }
          });
          _pendingReplies.insert(reply);
        }
      }
    }
  }
  if (_pendingReplies.isEmpty()) {
    QTimer::singleShot(0, this, &Updater::onUpdateNotNecessary); // While GUI is Idle
    _networkAccessManager->deleteLater();
    _networkAccessManager = nullptr;
  } else {
    _timeoutTimer->start(std::max(0, std::min(timeout, MaxDownloadTimeoutSeconds)) * 1000);
  }
  TIMING;
}

QList<QString> Updater::errorMessages() const
{
  return _errorMessages;
}

bool Updater::allDownloadsOk() const
{
  return _errorMessages.isEmpty();
}

void Updater::processReply(QNetworkReply * reply)
{
  const QString url = reply->request().url().toString();
  const QString filename = localFilename(url);
  if (filename.isEmpty()) {
    _errorMessages << QString(tr("Error downloading %1 (cannot determine a local file name)")).arg(url);
    return;
  }
  QByteArray array = reply->readAll();
  if (array.isEmpty()) {
    // Empty body with no network error (e.g. an HTTP redirection that is not followed)
    _errorMessages << QString(tr("Error downloading %1 (empty file?)")).arg(url);
    touchFile(filename); // Prevent a systematic update on next startups (see onNetworkReplyFinished())
    return;
  }
  if (array.size() > MaxSourceFileSize) {
    _errorMessages << tr("Error downloading %1<br/>File is too large").arg(url);
    touchFile(filename);
    return;
  }
  if (isCImgCompressed(array)) {
    TRACE << QString("Decompressing reply from") << url;
    array = cimgzDecompress(array);
  }
  if (array.isNull() || !array.contains("#@gui")) {
    _errorMessages << QString(tr("Could not read/decompress %1")).arg(url);
    touchFile(filename); // Prevent a systematic update on next startups (see onNetworkReplyFinished())
    return;
  }
  if (!safelyWrite(array, filename)) {
    _errorMessages << QString(tr("Error writing file %1")).arg(filename);
    return;
  }
  _someNetworkUpdatesAchieved = true;
}

void Updater::onNetworkReplyFinished(QNetworkReply * reply)
{
  TIMING;
  const QNetworkReply::NetworkError error = reply->error();
  if (error == QNetworkReply::NoError) {
    processReply(reply);
  } else {
    // Note: reply->url() may differ from the requested URL (redirection), and the local file name
    // is always the one of the requested URL.
    const QString url = reply->request().url().toString();
    if (reply->property(TooLargeProperty).toBool()) {
      _errorMessages << tr("Error downloading %1<br/>File is too large").arg(url);
    } else if (reply->property(TimedOutProperty).toBool()) {
      _errorMessages << QString(tr("Download timeout: %1")).arg(url);
    } else {
      QString str;
      QDebug d(&str);
      d << error;
      str = str.trimmed();
      _errorMessages << tr("Error downloading %1<br/>Error %2: %3").arg(url).arg(static_cast<int>(error)).arg(str);
    }
    Logger::error("Update failed");
    Logger::note(QString("Error string: %1").arg(reply->errorString()));
    // The body of an error reply is arbitrary (e.g. an HTML page): only log the beginning of it.
    Logger::note(QString("******* Reply contents (at most %1 bytes) ******\n").arg(MaxLoggedReplyBytes));
    Logger::note(QString::fromUtf8(reply->read(MaxLoggedReplyBytes)));
    Logger::note(QString("******** HTTP Status: %1").arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));
    // We either create an empty local file or 'touch' the existing one to prevent a systematic update on next startups
    // Instead, usual delay will occur before next try
    const QString filename = localFilename(url);
    if (!filename.isEmpty()) {
      touchFile(filename);
    }
  }
  _pendingReplies.remove(reply);
  if (_pendingReplies.isEmpty()) {
    _timeoutTimer->stop();
    if (_errorMessages.isEmpty()) {
      emit updateIsDone(static_cast<int>(UpdateStatus::Successful));
    } else {
      emit updateIsDone(static_cast<int>(UpdateStatus::SomeFailed));
    }
    if (_networkAccessManager) {
      _networkAccessManager->deleteLater();
      _networkAccessManager = nullptr;
    }
  }
  reply->deleteLater();
}

void Updater::notifyAllDownloadsOK()
{
  _errorMessages.clear();
  emit updateIsDone(static_cast<int>(UpdateStatus::Successful));
}

void Updater::cancelAllPendingDownloads()
{
  TIMING;
  // Make a copy because aborting will call onNetworkReplyFinished, and
  // thus modify the _pendingReplies set.
  const QSet<QNetworkReply *> replies = _pendingReplies;
  for (QNetworkReply * reply : replies) {
    // The error message is added by onNetworkReplyFinished(), so that it is not reported twice.
    reply->setProperty(TimedOutProperty, true);
    reply->abort();
  }
}

void Updater::onUpdateNotNecessary()
{
  // Errors may have occurred before any download was started (e.g. no local file name for a source).
  emit updateIsDone(static_cast<int>(_errorMessages.isEmpty() ? UpdateStatus::NotNecessary : UpdateStatus::SomeFailed));
}

QByteArray Updater::cimgzDecompress(const QByteArray & array)
{
  try {
    gmic_library::gmic_image<char> buffer(array.constData(), array.size(), 1, 1, 1, true);
    gmic_library::gmic_list<char> list = gmic_library::gmic_list<char>::get_unserialize(buffer);
    if (list.size() == 1) {
      const std::size_t size = list[0].size();
      if (size > MaxDecompressedSize) { // Also guarantees that the conversion to int below is safe
        Logger::warning("Updater::cimgzDecompress(): Decompressed data is too large");
        return {};
      }
      return {list[0].data(), static_cast<int>(size)};
    }
  } catch (...) {
    Logger::warning("Updater::cimgzDecompress(): Error decompressing data");
  }
  return {};
}

QByteArray Updater::cimgzDecompressFile(const QString & filename)
{
  gmic_library::gmic_image<unsigned char> buffer;
  try {
    buffer.load_cimg(filename.toLocal8Bit().constData());
  } catch (...) {
    Logger::warning("Updater::cimgzDecompressFile(): gmic_image<>::load_cimg error for file " + filename);
    return QByteArray();
  }
  const std::size_t size = buffer.size();
  if (size > MaxDecompressedSize) {
    // Beside limiting memory usage, this avoids a negative int size, which QByteArray would
    // interpret as "up to the first null character".
    Logger::warning("Updater::cimgzDecompressFile(): Decompressed data is too large for file " + filename);
    return QByteArray();
  }
  return QByteArray(reinterpret_cast<const char *>(buffer.data()), static_cast<int>(size));
}

QString Updater::localFilename(const QString & url)
{
  if (!url.startsWith("http://") && !url.startsWith("https://")) {
    return url;
  }
  QString directory = gmicConfigPath(true);
  if (directory.isEmpty()) {
    // Without this test, the "local" file would be located in the current working directory.
    Logger::error("Cannot find or create the G'MIC configuration folder");
    return QString();
  }
  if (!directory.endsWith(QChar('/')) && !directory.endsWith(QChar(92))) {
    directory.append(QChar('/'));
  }
  const QUrl u(url);
  // Keep a plain file name only: a backslash is not a path separator in a URL, but it is one on Windows.
  QString name = u.fileName();
  const int separator = std::max(name.lastIndexOf(QChar('/')), name.lastIndexOf(QChar(92)));
  if (separator != -1) {
    name = name.mid(separator + 1);
  }
  // Control characters (e.g. a decoded "%00") and characters that are forbidden in file names on some systems
  const QString forbiddenCharacters = QString::fromLatin1("<>:\"|?*");
  for (int i = 0; i < name.size(); ++i) {
    const QChar character = name.at(i);
    if ((character.unicode() < 0x20) || (character.unicode() == 0x7f) || forbiddenCharacters.contains(character)) {
      name[i] = QChar('_');
    }
  }
  if (name.isEmpty() || (name == ".") || (name == "..") || (name.toUtf8().size() > 200)) {
    // e.g. "https://example.com/": use a name derived from the URL, which is distinct for each source
    // (the URL itself is the name of the cache entry, so it must not be a directory).
    const QByteArray hash = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    name = QString("source_%1.gmic").arg(QString::fromLatin1(hash));
  }
  return directory + name;
}

bool Updater::appendLocalGmicFile(QByteArray & array, const QString & filename) const
{
  if (filename.isEmpty()) {
    return false;
  }
  const QFileInfo info(filename);
  // isFile() also rules out directories and special files (a FIFO would block forever when read).
  if (!info.isFile() || !info.size()) {
    return false;
  }
  if (info.size() > MaxSourceFileSize) {
    Logger::warning(QString("File is too large, ignored: %1").arg(filename));
    return false;
  }
  QFile file(filename);
  if (!file.open(QIODevice::ReadOnly)) {
    Logger::error("Error opening file: " + filename);
    return false;
  }
  QByteArray fileData;
  if (isCImgCompressed(file.peek(10))) {
    file.close();
    TRACE << "Appending compressed file:" << filename;
    fileData = cimgzDecompressFile(filename);
    if (!fileData.size()) {
      return false;
    }
  } else {
    TRACE << "Appending:" << filename;
    fileData = file.readAll();
    if (file.error() != QFileDevice::NoError) {
      Logger::error(QString("Error reading file %1 (%2)").arg(filename, file.errorString()));
      return false;
    }
  }
  if (static_cast<qint64>(array.size()) + fileData.size() + 1 > MaxFullStdlibSize) {
    Logger::warning(QString("The filters library would become too large, file ignored: %1").arg(filename));
    return false;
  }
  array.append(fileData);
  array.append('\n');
  return true;
}

void Updater::prependOfficialSourceIfRelevant(QStringList & list)
{
  if (Settings::officialFilterSource() == SourcesWidget::OfficialFilters::EnabledWithUpdates) {
    list.push_front(QString::fromUtf8(OfficialFilterSourceURL));
  }
}

QByteArray Updater::buildFullStdlib() const
{
  QByteArray result;
  const QByteArray ToTopLevelSeparator = QString("#@gui %1\n").arg(QString("_").repeated(80)).toUtf8();

  QStringList sources = GmicStdLib::substituteSourceVariables(Settings::filterSources());

  switch (Settings::officialFilterSource()) {
  case SourcesWidget::OfficialFilters::Disabled:
    // No stdlib included
    break;
  case SourcesWidget::OfficialFilters::EnabledWithoutUpdates:
    appendBuiltinGmicStdlib(result);
    result.append(ToTopLevelSeparator);
    break;
  case SourcesWidget::OfficialFilters::EnabledWithUpdates:
    if (!appendLocalGmicFile(result, localFilename(QString::fromUtf8(OfficialFilterSourceURL)))) {
      // Fallback on builtin stdlib
      appendBuiltinGmicStdlib(result);
    }
    result.append(ToTopLevelSeparator);
    break;
  }

  for (const QString & source : sources) {
    QString filename = localFilename(source);
    if (appendLocalGmicFile(result, filename)) {
      result.append(ToTopLevelSeparator);
    }
  }
  return result;
}

bool Updater::someNetworkUpdateAchieved() const
{
  return _someNetworkUpdatesAchieved;
}

void Updater::setOutputMessageMode(OutputMessageMode mode)
{
  _outputMessageMode = mode;
}

void Updater::appendBuiltinGmicStdlib(QByteArray & array) const
{
  gmic_image<char> stdlib_h = gmic::decompress_stdlib();
  if (!stdlib_h.size() || (stdlib_h.size() == 1)) {
    Logger::error("Could not decompress gmic builtin stdlib");
    return;
  }
  array.append(stdlib_h.data(), static_cast<int>(stdlib_h.size() - 1));
  array.append('\n');
}

} // namespace GmicQt
