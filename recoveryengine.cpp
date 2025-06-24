#include "recoveryengine.h"

#include <QString>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "Mp3.h"
#include "mp4.h"

using namespace std;
namespace fs = std::filesystem;

static const vector<unsigned char> PNG_SIGNATURE = {0x89, 0x50, 0x4E, 0x47,
                                                    0x0D, 0x0A, 0x1A, 0x0A};
static const vector<unsigned char> PNG_IEND = {
    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
static const vector<unsigned char> JPEG_SIGNATURE = {0xFF, 0xD8, 0xFF};
static const vector<unsigned char> JPEG_END = {0xFF, 0xD9};
static const vector<unsigned char> PDF_SIGNATURE = {0x25, 0x50, 0x44, 0x46,
                                                    0x2D};
static const vector<unsigned char> PDF_END = {0x25, 0x25, 0x45, 0x4F, 0x46};
static const vector<unsigned char> ZIP_SIGNATURE = {0x50, 0x4B, 0x03, 0x04};
static const vector<unsigned char> ZIP_END = {0x50, 0x4B, 0x05, 0x06};
static const vector<unsigned char> MP3_SIG = {0xFF, 0xE0};
static const vector<unsigned char> MP3_END = {0x00};
static const vector<unsigned char> DOC_SIGNATURE = {0xD0, 0xCF, 0x11, 0xE0,
                                                    0xA1, 0xB1, 0x1A, 0xE1};
static const vector<unsigned char> DOCX_SIGNATURE = {0x50, 0x4B, 0x03, 0x04};
static const vector<unsigned char> MP4_SIGNATURE = {0x00, 0x00, 0x00, 0x00,
                                                    0x66, 0x74, 0x79, 0x70};
static const vector<unsigned char> EXE_SIGNATURE = {0x4D, 0x5A};
static const vector<unsigned char> ELF_SIGNATURE = {0x7F, 0x45, 0x4C, 0x46};
static const vector<unsigned char> GENERIC_END = {0x00};

static const vector<vector<unsigned char>> SIGNATURES = {
    PNG_SIGNATURE, JPEG_SIGNATURE, PDF_SIGNATURE, ZIP_SIGNATURE, MP3_SIG,
    DOC_SIGNATURE, DOCX_SIGNATURE, MP4_SIGNATURE, EXE_SIGNATURE, ELF_SIGNATURE};
vector<int> File_Count = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const vector<vector<unsigned char>> END_MARKERS = {
    PNG_IEND, JPEG_END, PDF_END, ZIP_END, MP3_END,
    GENERIC_END, GENERIC_END, GENERIC_END, GENERIC_END, GENERIC_END};

static const vector<string> FILE_EXTENSIONS = {".png", ".jpg", ".pdf", ".zip",
                                               ".mp3", ".doc", ".docx", ".mp4",
                                               ".exe", ".elf"};

static const vector<string> FILE_NAMES = {"PNG", "JPEG", "PDF", "ZIP", "MP3",
                                          "DOC", "DOCX", "MP4", "EXE", "ELF"};

static const vector<pair<int, int>> Size_limit = {
    {512 * 2, 20 * 1024 * 1024}, {512 * 2, 20 * 1024 * 1024}, {1024, 50 * 1024 * 1024}, {1024, 100 * 1024 * 1024}, {1024, 20 * 1024 * 1024}, {1024, 50 * 1024 * 1024}, {1024, 50 * 1024 * 1024}, {1024, 500 * 1024 * 1024}, {1024, 50 * 1024 * 1024}, {1024, 50 * 1024 * 1024}};

static const int SupportedFileCount = 5;

RecoveryEngine::RecoveryEngine(const QString &inputDevice,
                               const QString &outputDir,
                               const std::vector<bool> &formats)
    : inputDevicePath(inputDevice),
      outputDirectory(outputDir),
      File_Supported(formats) {}

bool RecoveryEngine::matchesSignature(const vector<unsigned char> &buffer,
                                      size_t pos,
                                      const vector<unsigned char> &signature,
                                      const int formatIndex)
{
  if (pos + signature.size() > buffer.size())
    return false;
  for (size_t i = 0; i < signature.size(); ++i)
  {
    if (buffer[pos + i] != signature[i])
      return false;
  }
  if (formatIndex == 1)
  {
    if (!((buffer[3] & 0xF0) == 0xE0))
      return false;
  }
  return true;
}

void RecoveryEngine::extractFile(const QString &filename, size_t fileStart,
                                 int &fileCount, int formatIndex,
                                 std::function<void(QString)> logCallback)
{
  ifstream file(filename.toStdString(), ios::binary);
  if (!file)
  {
    logCallback("Error: Failed to reopen file.");
    return;
  }
  file.seekg(fileStart, ios::beg);

  size_t minSize = Size_limit[formatIndex].first;
  size_t maxSize = Size_limit[formatIndex].second;
  size_t chunkSize = 4 * 1024;
  vector<unsigned char> readBuffer(chunkSize);
  string outputDir = outputDirectory.toStdString();
  string dirPath = outputDir + "/" + FILE_NAMES[formatIndex];
  if (!fs::exists(dirPath))
  {
    logCallback("Creating directory: " + QString::fromStdString(dirPath));
    fs::create_directories(dirPath);
  }

  string outFileName = dirPath + "/RecoveredFile_" +
                       to_string(++File_Count[formatIndex]) +
                       FILE_EXTENSIONS[formatIndex];

  ofstream outFile(outFileName, ios::binary);
  if (!outFile)
  {
    logCallback("Error: Failed to create output file.");
    return;
  }

  bool xrefFound = false, trailerFound = false, foundEnd = false;
  size_t totalBytesWritten = 0;

  vector<unsigned char> PDF_XREF = {'x', 'r', 'e', 'f'};
  vector<unsigned char> PDF_TRAILER = {'t', 'r', 'a', 'i', 'l', 'e', 'r'};

  while (!foundEnd && (file.read(reinterpret_cast<char *>(readBuffer.data()),
                                 readBuffer.size()) ||
                       file.gcount() > 0))
  {
    size_t chunkBytes = file.gcount();
    size_t writeBytes = chunkBytes;

    if (END_MARKERS[formatIndex] == GENERIC_END)
    {
      for (size_t j = 0; j < SupportedFileCount; j++)
      {
        for (size_t k = 0; k + SIGNATURES[j].size() <= chunkBytes; k++)
        {
          if (matchesSignature(readBuffer, k, SIGNATURES[j], j))
          {
            writeBytes = k + SIGNATURES[j].size();
            foundEnd = true;
            break;
          }
        }
        if (foundEnd)
          break;
      }
    }
    else
    {
      for (size_t j = 0; j + END_MARKERS[formatIndex].size() <= chunkBytes;
           j++)
      {
        if (formatIndex == 2)
        {
          if (matchesSignature(readBuffer, j, PDF_XREF, 2))
            xrefFound = true;
          if (matchesSignature(readBuffer, j, PDF_TRAILER, 2))
            trailerFound = true;
        }
        if (matchesSignature(readBuffer, j, END_MARKERS[formatIndex],
                             formatIndex))
        {
          writeBytes = j + END_MARKERS[formatIndex].size();
          foundEnd = true;
          break;
        }
      }
    }

    outFile.write(reinterpret_cast<char *>(readBuffer.data()), writeBytes);
    totalBytesWritten += writeBytes;
    if (totalBytesWritten > maxSize)
    {
      goto DELETE;
    }
  }

  if (!foundEnd && formatIndex == 2 && xrefFound && trailerFound)
  {
    outFile.write(
        reinterpret_cast<const char *>(END_MARKERS[formatIndex].data()),
        END_MARKERS[formatIndex].size());
    foundEnd = true;
  }

  outFile.close();
  file.close();

  if (foundEnd &&
      (totalBytesWritten < minSize || totalBytesWritten > maxSize))
  {
  DELETE:
    // logCallback("[SKIP] Deleted file with invalid size: " +
    //             QString::fromStdString(FILE_NAMES[formatIndex]) + " (" + QString::number(totalBytesWritten) + " bytes)");
    remove(outFileName.c_str());
    fileCount--;
    return;
  }

  if (!foundEnd || (formatIndex == 2 && (!xrefFound || !trailerFound)))
  {
    logCallback("[SKIP] Deleted incomplete file: " +
                QString::fromStdString(outFileName));
    remove(outFileName.c_str());
    fileCount--;
    return;
  }

  logCallback("[OK] Recovered: " + QString::fromStdString(outFileName));
}

bool RecoveryEngine::run(std::function<void(QString)> logCallback,
                         std::function<void(int)> progressCallback,
                         std::function<bool()> cancelCheck)
{
  const string filename = inputDevicePath.toStdString();
  const size_t CHUNK_SIZE = 4096;

  ifstream file(filename, ios::binary);
  if (!file)
  {
    logCallback("Error: Failed to open file.");
    return false;
  }

  file.seekg(0, ios::end);
  size_t fileSize = file.tellg();
  file.seekg(0, ios::beg);

  logCallback("File size: " + QString::number(fileSize) + " bytes");

  // Mp3 mp3(outputDirectory);
  Mp3 mp3(outputDirectory.toStdString());
  MP4 mp4;
  int fileCount = 0;
  size_t offset = 0;
  size_t mp3_offset_done = 0;
  size_t overlap = 0;
  vector<unsigned char> buffer(CHUNK_SIZE + overlap);

  while (file.read(reinterpret_cast<char *>(buffer.data() + overlap),
                   CHUNK_SIZE) ||
         file.gcount() > 0)
  {
    if (cancelCheck())
    {
      logCallback("[!] Operation cancelled.");
      return false;
    }
    size_t bytesRead = file.gcount();
    for (int formatIndex = 0; formatIndex < SupportedFileCount; formatIndex++)
    {
      if (!File_Supported[formatIndex])
        continue;
      for (size_t i = 0;
           i + SIGNATURES[formatIndex].size() <= bytesRead + overlap; i++)
      {
        size_t fileStart = offset + i;
        if (formatIndex == 4 && mp3.matchesMP3Header(buffer, i) &&
            offset + i >= mp3_offset_done)
        {
          // logCallback("Found MP3 at offset: " + QString::number(fileStart));
          // cout << "entered mp3 extraction" << endl;
          mp3_offset_done = mp3.extractMP3File(filename, fileStart,
                                               ++File_Count[formatIndex], logCallback, cancelCheck);
          i += 4;
          // cout << "extracted mp3 file" << endl;
        }
        else if (formatIndex == 7 &&
                 mp4.matchesMP4Header(buffer, MP4_SIGNATURE, i))
        {
          // logCallback("Found MP4 at offset: " + QString::number(fileStart));
          mp4.extractMP4File(filename, fileStart, ++fileCount);
          i += 8;
        }
        else if (formatIndex != 4 && formatIndex != 7 &&
                 matchesSignature(buffer, i, SIGNATURES[formatIndex],
                                  formatIndex))
        {
          // logCallback("Found Signature at offset: " +
          //             QString::number(fileStart));
          extractFile(QString::fromStdString(filename), fileStart, ++fileCount,
                      formatIndex, logCallback);
          i += SIGNATURES[formatIndex].size();
        }
      }
    }

    offset += bytesRead;
    if (fileSize > 0)
    {
      int progress =
          static_cast<int>((static_cast<double>(offset) / fileSize) * 100);
      progressCallback(progress);
    }
    if (bytesRead == CHUNK_SIZE && overlap > 0)
    {
      for (int i = 0; i < overlap; ++i)
      {
        buffer[i] = buffer[CHUNK_SIZE + i - overlap];
      }
    }
  }

  file.close();

  logCallback("File recovery summary:");
  logCallback("Total files recovered: " + QString::number(fileCount));

  for (int i = 0; i < SupportedFileCount; i++)
  {
    if (!File_Supported[i])

      continue;
    if (File_Count[i] > 0)
    {
      logCallback(QString::fromStdString(FILE_NAMES[i]) + ": " +
                  QString::number(File_Count[i]) + " files recovered.");
    }
    else
    {
      logCallback(QString::fromStdString(FILE_NAMES[i]) + ": No files found.");
    }
  }

  return true;
}
