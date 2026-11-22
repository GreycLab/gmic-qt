/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file FiltersModelBinaryReader.h
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
#ifndef GMIC_QT_FILTERSMODELBINARYREADER_H
#define GMIC_QT_FILTERSMODELBINARYREADER_H

#include <QByteArray>
#include <QDataStream>
#include <QList>
#include <QString>
#include "FilterSelector/FiltersModel.h"

namespace GmicQt
{

class FiltersModelBinaryReader {
public:
  explicit FiltersModelBinaryReader(FiltersModel & model);

  /**
   * Fill the model from a binary cache file.
   *
   * The model is left untouched if the file cannot be read entirely and
   * successfully (missing file, wrong magic number, truncated or corrupted
   * content...), so that the caller can safely fall back on parsing the
   * filter definitions.
   */
  bool read(const QString & filename);

  static QByteArray readHash(const QString & filename);

private:
  FiltersModel & _model;
  static bool readHeader(QDataStream & stream, QByteArray & hash);
  static bool readString(QDataStream & stream, QString & string);
  static bool readStringList(QDataStream & stream, QList<QString> & list);
  static bool readFilter(QDataStream & stream, FiltersModel::Filter & filter);
  static bool isValidInputMode(quint8 value);
};

} // namespace GmicQt

#endif // GMIC_QT_FILTERSMODELBINARYREADER_H
