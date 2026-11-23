/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file SearchFieldWidget.cpp
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
#include "Widgets/SearchFieldWidget.h"
#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QPalette>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QLayout>
#include "Common.h"
#include "IconLoader.h"
#include "Settings.h"
#include "ui_SearchFieldWidget.h"

namespace GmicQt
{

SearchFieldWidget::SearchFieldWidget(QWidget * parent) : QWidget(parent), ui(new Ui::SearchFieldWidget)
{
  ui->setupUi(this);
  _clearIcon = IconLoader::load("edit-clear");
  _findIcon = IconLoader::load("edit-find");
  _empty = true;

  // _lineEdit used to be left uninitialized (and then used) when the layout was not a QHBoxLayout.
  _lineEdit = new QLineEdit(this);
  QLayout * mainLayout = layout();
  if (!mainLayout) {
    mainLayout = new QHBoxLayout(this);
  }
  if (auto hbox = dynamic_cast<QHBoxLayout *>(mainLayout)) {
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setSpacing(0);
  }
  mainLayout->addWidget(_lineEdit);
  _action = _lineEdit->addAction(_findIcon, QLineEdit::TrailingPosition);
  connect(_action, &QAction::triggered, _lineEdit, &QLineEdit::clear);
  connect(_lineEdit, &QLineEdit::textChanged, this, &SearchFieldWidget::textChanged);
  connect(_lineEdit, &QLineEdit::textChanged, this, &SearchFieldWidget::onTextChanged);
  _lineEdit->setPlaceholderText(tr("Search"));
  _lineEdit->setToolTip(tr("Search in filters list (%1)").arg(QKeySequence(QKeySequence::Find).toString()));
  setFocusProxy(_lineEdit);
#if QT_VERSION_GTE(5, 12, 0)
  if (Settings::darkThemeEnabled()) {
    QPalette palette = _lineEdit->palette();
    palette.setColor(QPalette::PlaceholderText, Qt::gray);
    _lineEdit->setPalette(palette);
  }
#endif
  auto validator = new QRegularExpressionValidator(QRegularExpression("[^/].*"), this);
  _lineEdit->setValidator(validator);
}

SearchFieldWidget::~SearchFieldWidget()
{
  delete ui;
}

QString SearchFieldWidget::text() const
{
  return _lineEdit->text();
}

void SearchFieldWidget::onTextChanged(const QString & str)
{
  if (str.isEmpty()) {
    _empty = true;
    _action->setIcon(_findIcon);
  } else {
    if (_empty) {
      _action->setIcon(_clearIcon);
    }
    _empty = false;
  }
}

void SearchFieldWidget::clear()
{
  _lineEdit->clear();
}

} // namespace GmicQt


