#include "mainwindow.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QStorageInfo>
#include <QtConcurrent>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "../recoveryengine.h"  // adjust path as needed
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
  ui->setupUi(this);
  ui->progressBar->setValue(0);
  ui->progressBar->setRange(0, 100);
  fileTypeCheckboxes = {ui->checkBoxJPEG, ui->checkBoxPNG, ui->checkBoxMP3,
                        ui->checkBoxPDF, ui->checkBoxZIP};
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::on_selectDirButton_clicked() {
  QDir devDir("/dev");
  devDir.setFilter(QDir::System | QDir::Readable | QDir::Files);
  devDir.setNameFilters(QStringList()
                        << "sd?" << "nvme?n1" << "mmcblk0" << "loop*");

  QStringList entries = devDir.entryList();
  QStringList devicePaths;
  for (const QString &entry : entries) {
    // std::cout << entry.toStdString() << std::endl;
    devicePaths << "/dev/" + entry;
  }

  if (devicePaths.isEmpty()) {
    QMessageBox::information(
        this, "No Devices",
        "No raw block devices found (e.g., /dev/sdX, loop, etc.).");
    return;
  }

  bool ok;
  QString selectedDevice = QInputDialog::getItem(
      this, "Select Raw Device", "Choose a device to scan:", devicePaths, 0,
      false, &ok);
  if (ok && !selectedDevice.isEmpty()) {
    selectedDir = selectedDevice;
    ui->logBox->append("Selected device: " + selectedDir);
  }
}

void MainWindow::on_selectOutputButton_clicked() {
  outputDir = QFileDialog::getExistingDirectory(this, "Select Output Folder");
  if (!outputDir.isEmpty()) {
    ui->logBox->append("Selected output folder: " + outputDir);
  }
}

void MainWindow::on_startRecoveryButton_clicked() {
  std::vector<bool> File_Supported(10, false);
  QStringList selectedFormats;

  std::map<std::string, int> File_Supported_Map = {
      {"     PNG", 0}, {"     JPEG", 1}, {"     PDF", 2},  {"     ZIP", 3},
      {"     MP3", 4}, {"     DOC", 5},  {"     DOCX", 6}, {"     MP4", 7},
      {"     EXE", 8}, {"     ELF", 9}};

  for (QCheckBox *cb : fileTypeCheckboxes) {
    if (cb->isChecked()) {
      selectedFormats << cb->text();
      if (File_Supported_Map.find(cb->text().toStdString()) ==
          File_Supported_Map.end()) {
        QMessageBox::warning(this, "Unsupported Format",
                             "The selected format '" + cb->text() +
                                 "' is not supported for recovery.");
        return;
      }
      File_Supported[File_Supported_Map[cb->text().toStdString()]] = true;
    }
  }

  if (selectedFormats.isEmpty()) {
    QMessageBox::warning(this, "No Formats",
                         "Please select at least one file format to recover.");
    return;
  }
  if (selectedDir.isEmpty()) {
    QMessageBox::warning(this, "No Drive", "Please select the drive to scan.");
    return;
  }
  if (outputDir.isEmpty()) {
    QMessageBox::warning(this, "No Output Folder",
                         "Please select where to save recovered files.");
    return;
  }

  ui->logBox->append("Starting recovery...");
  ui->logBox->append("From: " + selectedDir);
  ui->logBox->append("To: " + outputDir);
  ui->logBox->append("Formats: " + selectedFormats.join(", "));

  ui->progressBar->setValue(0);
  ui->startRecoveryButton->setEnabled(false);
  ui->cancelRecoveryButton->setEnabled(true);
  cancelRequested = false;

  QtConcurrent::run([=]() {
    RecoveryEngine engine(selectedDir, outputDir, File_Supported);

    auto logCallback = [=](const QString &msg) {
      QMetaObject::invokeMethod(ui->logBox, "append", Qt::QueuedConnection,
                                Q_ARG(QString, msg));
    };

    auto progressCallback = [=](int percent) {
      QMetaObject::invokeMethod(ui->progressBar, "setValue",
                                Qt::QueuedConnection, Q_ARG(int, percent));
    };

    auto cancelCheck = [=]() -> bool { return cancelRequested.load(); };

    bool success = engine.run(logCallback, progressCallback, cancelCheck);

    QMetaObject::invokeMethod(
        this,
        [=]() {
          ui->startRecoveryButton->setEnabled(true);
          ui->cancelRecoveryButton->setEnabled(false);
          ui->logBox->append(success ? "Recovery completed successfully."
                                     : "Recovery was cancelled.");
          if (!success) ui->progressBar->setValue(0);
        },
        Qt::QueuedConnection);
  });
}

void MainWindow::on_cancelRecoveryButton_clicked() {
  cancelRequested = true;
  ui->logBox->append("[!] Cancel requested by user.");
  ui->cancelRecoveryButton->setEnabled(false);
}
