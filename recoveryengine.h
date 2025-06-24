#ifndef RECOVERYENGINE_H
#define RECOVERYENGINE_H

#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

class RecoveryEngine {
 public:
  RecoveryEngine(const QString &inputDevice, const QString &outputDir,
                 const std::vector<bool> &formats);

  bool run(std::function<void(QString)> logCallback,
           std::function<void(int)> progressCallback,
           std::function<bool()> cancelCheck);

 private:
  bool matchesSignature(const std::vector<unsigned char> &buffer, size_t pos,
                        const std::vector<unsigned char> &signature,
                        int formatIndex);
  void extractFile(const QString &filename, size_t fileStart, int &fileCount,
                   int formatIndex, std::function<void(QString)> logCallback);

  QString inputDevicePath;
  QString outputDirectory;
  std::vector<bool> File_Supported;
};

#endif  // RECOVERYENGINE_H
