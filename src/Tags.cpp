/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file Tags.cpp
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
#include "Tags.h"
#include "Common.h"

#include <QAction>
#include <QBuffer>
#include <QByteArray>
#include <QDebug>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include "DialogSettings.h"

namespace GmicQt
{

const TagColorSet TagColorSet::Full(TagColorSet::_fullMask);
const TagColorSet TagColorSet::ActualColors(TagColorSet::_fullMask &(~1u));
const TagColorSet TagColorSet::Empty(0);

const char * TagColorNames[] = {"None", "Red", "Green", "Blue", "Cyan", "Magenta", "Yellow"};

QString TagAssets::_markerHtml[static_cast<unsigned int>(TagColor::Count)];
QIcon TagAssets::_menuIcons[static_cast<unsigned int>(TagColor::Count)];
QIcon TagAssets::_menuIconsWithCheck[static_cast<unsigned int>(TagColor::Count)];
QIcon TagAssets::_menuIconsWithDisk[static_cast<unsigned int>(TagColor::Count)];
unsigned int TagAssets::_markerSideSize[static_cast<unsigned int>(TagColor::Count)];
QColor TagAssets::colors[static_cast<unsigned int>(TagColor::Count)] = {QColor(0, 0, 0, 0),    QColor(250, 68, 113),  QColor(179, 228, 59), QColor(121, 171, 255),
                                                                        QColor(117, 225, 242), QColor(188, 154, 234), QColor(236, 224, 105)};

const QString & TagAssets::markerHtml(const TagColor color, unsigned int height)
{
  if (!(height % 2)) {
    ++height;
  }
  const int iColor = (int)color;
  if (!_markerHtml[iColor].isEmpty() && _markerSideSize[iColor] == height) {
    return _markerHtml[iColor];
  }
  QImage image(height, height, QImage::Format_RGBA8888);
  image.fill(QColor(0, 0, 0, 0));
  if (color != TagColor::None) {
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen = painter.pen();
    pen.setWidth(1);
    pen.setColor(QColor(0, 0, 0, 128));
    painter.setPen(pen);
    painter.setBrush(colors[iColor]);
    painter.drawEllipse(1, 1, height - 2, height - 2);
  }
  QByteArray ba;
  QBuffer buffer(&ba);
  image.save(&buffer, "png");
  _markerSideSize[iColor] = height;
  _markerHtml[iColor] = QString("<img style=\"vertical-align: baseline\" src=\"data:image/png;base64,%1\"/>").arg(QString(ba.toBase64()));
  return _markerHtml[iColor];
}

const QIcon & TagAssets::menuIcon(TagColor color, IconMark mark)
{
  const int iColor = (int)color;
  if (_menuIcons[iColor].isNull()) {
    QPixmap bg(64, 64);
    QFont font;
    font.setPixelSize(60);
    {
      bg.fill(QColor(0, 0, 0, 0));
      QPainter p(&bg);
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setBrush(colors[iColor]);
      p.drawRoundedRect(bg.rect(), 15, 15);
      _menuIcons[iColor] = QIcon(bg);
    }
    QPixmap pixmap(bg);
    {
      QPainter p(&pixmap);
      p.setFont(font);
      p.setPen(Qt::black);
      p.setBrush(Qt::black);
      p.setRenderHint(QPainter::Antialiasing, true);
      p.drawText(pixmap.rect(), Qt::AlignCenter | Qt::AlignVCenter, "\xE2\x9C\x93"); // CHECK MARK
      _menuIconsWithCheck[iColor] = QIcon(pixmap);
    }
    pixmap = bg;
    {
      QPainter p(&pixmap);
      p.setFont(font);
      p.setPen(Qt::gray);
      p.drawText(pixmap.rect(), Qt::AlignCenter | Qt::AlignVCenter, "\xE2\x9A\xAB"); // MEDIUM BLACK CIRCLE
      _menuIconsWithDisk[iColor] = QIcon(pixmap);
    }
  }
  switch (mark) {
  case IconMark::Check:
    return _menuIconsWithCheck[iColor];
  case IconMark::Disk:
    return _menuIconsWithDisk[iColor];
  default:
    return _menuIcons[iColor];
  }
}

QAction * TagAssets::action(QObject * parent, TagColor color, IconMark mark)
{
  if ((color == TagColor::None) || (color == TagColor::Count)) {
    return nullptr;
  }
  return new QAction(menuIcon(color, mark), "Tag", parent);
}

std::ostream & operator<<(std::ostream & out, const TagColorSet & colors)
{
  out << "{";
  bool first = true;
  for (TagColor color : colors) {
    if (first) {
      first = false;
    } else {
      out << ",";
    }
    out << TagColorNames[int(color)];
  }
  out << "}";
  return out;
}

} // namespace GmicQt