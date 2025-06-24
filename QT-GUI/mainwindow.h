#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QCheckBox>
#include <QFuture>
#include <QMainWindow>
#include <QtConcurrent>
#include <atomic>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

 private slots:
  void on_selectDirButton_clicked();
  void on_selectOutputButton_clicked();
  void on_cancelRecoveryButton_clicked();
  void on_startRecoveryButton_clicked();

 private:
  Ui::MainWindow *ui;
  std::atomic_bool cancelRequested = false;
  QString selectedDir;
  QString outputDir;
  QList<QCheckBox *> fileTypeCheckboxes;
};

#endif  // MAINWINDOW_H
