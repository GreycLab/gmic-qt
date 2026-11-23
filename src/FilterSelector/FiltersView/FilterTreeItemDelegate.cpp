/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file FilterTreeItemDelegate.cpp
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
#include "FilterTreeItemDelegate.h"
#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QPainter>
#include <QPalette>
#include <QStandardItemModel>
#include <QStyle>
#include <QTextDocument>
#include "FilterSelector/FiltersView/FilterTreeAbstractItem.h"
#include "FilterSelector/FiltersView/FilterTreeItem.h"
#include "Settings.h"
#include "Tags.h"

namespace GmicQt
{

FilterTreeItemDelegate::FilterTreeItemDelegate(QObject * parent) : QStyledItemDelegate(parent) {}

void FilterTreeItemDelegate::paint(QPainter * painter, const QStyleOptionViewItem & option, const QModelIndex & index) const
{
  auto model = dynamic_cast<const QStandardItemModel *>(index.model());
  const QStandardItem * item = model ? model->itemFromIndex(index) : nullptr;
  Q_ASSERT_X(item, "FiltersTreeItemDelegate::paint()", "No item");
  if (!item) {
    // Not the expected kind of model (the assertion above is not active in release mode)
    QStyledItemDelegate::paint(painter, option, index);
    return;
  }
  QStyleOptionViewItem options = option;
  initStyleOption(&options, index);
  painter->save();

  auto filter = dynamic_cast<const FilterTreeItem *>(item);
  const int height = int(options.rect.height() * 0.4);
  QString tagString;

  if (filter) {
    TagColorSet tags = filter->tags();
    if (!tags.isEmpty()) {
      tagString = "&nbsp;&nbsp;";
      for (TagColor color : tags) {
        tagString += QString("&nbsp;") + TagAssets::markerHtml(color, height);
      }
    }
  }

  QTextDocument doc;
  if (!item->isCheckable() && filter && !filter->isVisible()) {
    QColor textColor;
    textColor = Settings::UnselectedFilterTextColor;
    // Multi-argument form: the substituted texts are never scanned for place markers (%1, %2...)
    doc.setHtml(QString("<span style=\"color:%1\">%2</span>&nbsp;%3").arg(textColor.name(), options.text, tagString));
  } else {
    if (filter) {
      doc.setHtml(options.text + tagString);
    } else {
      doc.setHtml(options.text);
    }
  }
  options.text = "";
  // The widget may be null (e.g. when the item is painted outside of a view)
  QStyle * style = options.widget ? options.widget->style() : QApplication::style();
  style->drawControl(QStyle::CE_ItemViewItem, &options, painter);
  painter->translate(options.rect.left(), options.rect.top());
  QRect clip(0, 0, options.rect.width(), options.rect.height());
  doc.drawContents(painter, clip);
  painter->restore();
}

QSize FilterTreeItemDelegate::sizeHint(const QStyleOptionViewItem & option, const QModelIndex & index) const
{
  QStyleOptionViewItem options = option;
  initStyleOption(&options, index);

  QTextDocument doc;
  doc.setHtml(options.text);
  doc.setTextWidth(options.rect.width());
  return {static_cast<int>(doc.idealWidth()), static_cast<int>(doc.size().height())};
}

} // namespace GmicQt


