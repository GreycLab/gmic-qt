#include "JpegQualityDialog.h"
#include <QSettings>
#include <algorithm>
#include "Settings.h"
#include "ui_jpegqualitydialog.h"
int JpegQualityDialog::_permanentQuality = -1;
int JpegQualityDialog::_selectedQuality = -1;

#define JPEG_QUALITY_KEY "Config/host_standalone/DefaultJpegQuality"

JpegQualityDialog::JpegQualityDialog(QWidget * parent) : QDialog(parent), ui(new Ui::JpegQualityDialog)
{
  ui->setupUi(this);
  setWindowTitle(tr("JPEG Quality"));
  ui->slider->setRange(0, 100);
  ui->spinBox->setRange(0, 100);

  if (_selectedQuality == -1) {
    _selectedQuality = std::max(0, std::min(100, QSettings().value(JPEG_QUALITY_KEY, 85).toInt()));
  }

  ui->slider->setValue(_selectedQuality);
  ui->spinBox->setValue(_selectedQuality);
  ui->pbOk->setDefault(true);
  connect(ui->slider, &QSlider::valueChanged, ui->spinBox, &QSpinBox::setValue);
  connect(ui->spinBox, QOverload<int>::of(&QSpinBox::valueChanged), ui->slider, &QSlider::setValue);
  connect(ui->pbOk, &QPushButton::clicked, this, [this]() {
    _selectedQuality = ui->spinBox->value();
    QSettings().setValue(JPEG_QUALITY_KEY, _selectedQuality);
    // The permanent quality is set here, once the choice is validated: it used to be set when the box
    // was ticked, i.e. with a quality which could still change, or even be cancelled.
    if (ui->makePermanent->isChecked()) {
      _permanentQuality = _selectedQuality;
    }
  });
  connect(ui->pbOk, &QPushButton::clicked, this, &QDialog::accept);
  connect(ui->pbCancel, &QPushButton::clicked, this, &QDialog::reject);

  if (GmicQt::Settings::darkThemeEnabled()) {
    QPalette p = ui->makePermanent->palette();
    p.setColor(QPalette::Text, GmicQt::Settings::CheckBoxTextColor);
    p.setColor(QPalette::Base, GmicQt::Settings::CheckBoxBaseColor);
    ui->makePermanent->setPalette(p);
  }
}

JpegQualityDialog::~JpegQualityDialog()
{
  delete ui;
}

int JpegQualityDialog::quality() const
{
  return ui->slider->value();
}

void JpegQualityDialog::setQuality(int q)
{
  if (q != -1) {
    ui->slider->setValue(q);
    ui->spinBox->setValue(q);
  }
}

int JpegQualityDialog::ask(QWidget * parent, int value)
{
  if (_permanentQuality != -1) {
    return _permanentQuality;
  }
  JpegQualityDialog dialog(parent); // Modal and local: no need to allocate it
  dialog.setQuality(value);
  return (dialog.exec() == QDialog::Accepted) ? dialog.quality() : -1;
}


