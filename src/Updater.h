/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file Updater.h
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
#ifndef GMIC_QT_UPDATER_H
#define GMIC_QT_UPDATER_H

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QTemporaryFile>
#include <QTimer>
#include <memory>
#include "GmicQt.h"

namespace GmicQt
{
class Updater : public QObject {
  Q_OBJECT

public:
  enum class UpdateStatus
  {
    Successful,
    SomeFailed,
    NotNecessary
  };

  static Updater * getInstance();
  static void setOutputMessageMode(OutputMessageMode mode);
  ~Updater() override;

  /**
   * @brief Launch download of files that are either not present locally, or
   *        older than the given age limit (in hours). To force download
   *        of all the sources, set the age limit to zero.
   *
   * @param ageLimit Delay between 2 network updates in hours
   * @param timeout in seconds before aborting downloads
   *
   * Nothing is done if an update is already in progress: updateIsDone() will
   * be emitted when that update ends.
   * @param useNetwork Enable internet access
   */
  void startUpdate(int ageLimit, int timeout, bool useNetwork);

  QList<QString> errorMessages() const;
  bool allDownloadsOk() const;
  QByteArray buildFullStdlib() const;

  bool someNetworkUpdateAchieved() const;

signals:
  void updateIsDone(int status);

public slots:
  void onNetworkReplyFinished(QNetworkReply *);
  void notifyAllDownloadsOK();
  void cancelAllPendingDownloads();
  void onUpdateNotNecessary();

protected:
  void processReply(QNetworkReply * reply);

private:
  /**
   * @brief Local file name used to store a source (a plain file name is returned as is, and
   *        for a URL it is a file of the G'MIC configuration folder).
   *        An empty string is returned if no suitable local name can be determined.
   */
  static QString localFilename(const QString & url);
  void appendBuiltinGmicStdlib(QByteArray & array) const;
  bool appendLocalGmicFile(QByteArray & array, const QString & filename) const;
  void prependOfficialSourceIfRelevant(QStringList & list);
  explicit Updater(QObject * parent);
  static bool isCImgCompressed(const QByteArray & data);
  static QByteArray cimgzDecompress(const QByteArray & array);
  static QByteArray cimgzDecompressFile(const QString & filename);
  static std::unique_ptr<Updater> _instance;
  static OutputMessageMode _outputMessageMode;
  static const char * OfficialFilterSourceURL;

  QNetworkAccessManager * _networkAccessManager;
  QTimer * _timeoutTimer; // Aborts the pending downloads when the delay given to startUpdate() is over
  QSet<QNetworkReply *> _pendingReplies;
  QList<QString> _errorMessages;
  bool _someNetworkUpdatesAchieved;
};

} // namespace GmicQt

#endif // GMIC_QT_UPDATER_H
