#include <cstdint>
#include <filesystem>  // For creating directories
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "mp3.h"
#include "mp4.h"

using namespace std;
namespace fs = std::filesystem;

const vector<unsigned char> PNG_SIGNATURE = {0x89, 0x50, 0x4E, 0x47,
                                             0x0D, 0x0A, 0x1A, 0x0A};
const vector<unsigned char> PNG_IEND = {0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
                                        0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

const vector<unsigned char> JPEG_SIGNATURE = {
    0xFF, 0xD8, 0xFF};  // changed to 0xFF,0xD8,0xFF from 0xFF,0xD8,
const vector<unsigned char> JPEG_END = {0xFF, 0xD9};  // JPEG EOI

const vector<unsigned char> PDF_SIGNATURE = {0x25, 0x50, 0x44, 0x46,
                                             0x2D};                    // %PDF-
const vector<unsigned char> PDF_END = {0x25, 0x25, 0x45, 0x4F, 0x46};  // %%EOF

const vector<unsigned char> ZIP_SIGNATURE = {0x50, 0x4B, 0x03,
                                             0x04};  // PK\x03\x04
const vector<unsigned char> ZIP_END = {0x50, 0x4B, 0x05,
                                       0x06};  // End of Central Directory

const vector<unsigned char> MP3_SIG = {
    0xFF, 0xE0};  // Simplified MP3 frame header start
const vector<unsigned char> MP3_END = {0x00};  // Placeholder

const vector<unsigned char> DOC_SIGNATURE = {
    0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};  // Old .doc/.xls
const vector<unsigned char> DOCX_SIGNATURE = {
    0x50, 0x4B, 0x03, 0x04};  // Same as ZIP, check internal [Content_Types].xml
const vector<unsigned char> MP4_SIGNATURE = {0x00, 0x00, 0x00, 0x00, 0x66,
                                             0x74, 0x79, 0x70};  // 'ftyp' box
const vector<unsigned char> EXE_SIGNATURE = {0x4D, 0x5A};        // "MZ" header
const vector<unsigned char> ELF_SIGNATURE = {0x7F, 0x45, 0x4C,
                                             0x46};  // 0x7F 'E' 'L' 'F'

// Optional dummy end markers (can improve or scan length-based)
const vector<unsigned char> GENERIC_END = {
    0x00};  // Placeholder for types with no clear EOF marker

vector<vector<unsigned char>> SIGNATURES = {
    PNG_SIGNATURE, JPEG_SIGNATURE, PDF_SIGNATURE, ZIP_SIGNATURE, MP3_SIG,
    DOC_SIGNATURE, DOCX_SIGNATURE, MP4_SIGNATURE, EXE_SIGNATURE, ELF_SIGNATURE};

vector<vector<unsigned char>> END_MARKERS = {
    PNG_IEND,    JPEG_END, PDF_END, ZIP_END, MP3_END,
    GENERIC_END,  // DOC
    GENERIC_END,  // DOCX
    GENERIC_END,  // MP4
    GENERIC_END,  // EXE
    GENERIC_END   // ELF
};

vector<string> FILE_EXTENSIONS = {".png", ".jpg",  ".pdf", ".zip", ".mp3",
                                  ".doc", ".docx", ".mp4", ".exe", ".elf"};
vector<string> FILE_NAMES = {"PNG", "JPEG", "PDF", "ZIP", "MP3",
                             "DOC", "DOCX", "MP4", "EXE", "ELF"};
vector<pair<int, int>> Size_limit = {
    {512, 20 * 1024 * 1024},   {512, 20 * 1024 * 1024},
    {1024, 50 * 1024 * 1024},  {1024, 100 * 1024 * 1024},
    {1024, 20 * 1024 * 1024},  {1024, 50 * 1024 * 1024},  // DOC
    {1024, 50 * 1024 * 1024},                             // DOCX
    {1024, 500 * 1024 * 1024},                            // MP4
    {1024, 50 * 1024 * 1024},                             // EXE
    {1024, 50 * 1024 * 1024}                              // ELF
};
int SupportedFileCount = 5;
bool matchesSignature(const vector<unsigned char> &buffer, size_t pos,
                      const vector<unsigned char> &signature) {
  if (pos + signature.size() > buffer.size()) return false;
  for (size_t i = 0; i < signature.size(); i++) {
    if (buffer[pos + i] != signature[i]) return false;
  }
  return true;
}
void extractFile(const string &filename, size_t fileStart, int &fileCount,
                 int formatIndex) {
  if (formatIndex == 4) return;  // MP3 handled elsewhere

  ifstream file(filename, ios::binary);
  if (!file) {
    cerr << "Failed to reopen file for extraction\n";
    return;
  }

  file.clear();  // Clear any flags
  file.seekg(fileStart, ios::beg);

  size_t chunkSize = 4 * 1024;
  vector<unsigned char> readBuffer(chunkSize);

  string outFileName = "./RecoveredData/" + FILE_NAMES[formatIndex] +
                       "/RecoveredFile_" + to_string(fileCount) +
                       FILE_EXTENSIONS[formatIndex];
  ofstream outFile(outFileName, ios::binary);
  if (!outFile) {
    cerr << "Failed to create output file " << outFileName << endl;
    return;
  }

  // PDF specific
  bool xrefFound = false;
  bool trailerFound = false;
  vector<unsigned char> PDF_XREF = {0x78, 0x72, 0x65, 0x66};  // xref
  vector<unsigned char> PDF_TRAILER = {0x74, 0x72, 0x61, 0x69,
                                       0x6C, 0x65, 0x72};  // trailer

  bool foundEnd = false;
  size_t totalBytesWritten = 0;

  while (!foundEnd && (file.read(reinterpret_cast<char *>(readBuffer.data()),
                                 readBuffer.size()) ||
                       file.gcount() > 0)) {
    size_t chunkBytes = file.gcount();
    size_t writeBytes = chunkBytes;

    if (END_MARKERS[formatIndex] == GENERIC_END) {
      for (size_t j = 0; j < SupportedFileCount; j++) {
        if (j >= 5 && j <= 9) continue;
        for (size_t k = 0; k + SIGNATURES[j].size() <= chunkBytes; k++) {
          if (matchesSignature(readBuffer, k, SIGNATURES[j])) {
            writeBytes = k + SIGNATURES[j].size();
            foundEnd = true;
            break;
          }
        }
        if (foundEnd) break;
      }
    } else {
      for (size_t j = 0; j + END_MARKERS[formatIndex].size() <= chunkBytes;
           j++) {
        if (formatIndex == 2 && (!xrefFound || !trailerFound))  // PDF specific
        {
          if (matchesSignature(readBuffer, j, PDF_XREF)) {
            xrefFound = true;
          }
          if (matchesSignature(readBuffer, j, PDF_TRAILER)) {
            trailerFound = true;
          }
        }
        if (matchesSignature(readBuffer, j, END_MARKERS[formatIndex])) {
          writeBytes = j + END_MARKERS[formatIndex].size();
          foundEnd = true;
          break;
        }
      }
    }

    outFile.write(reinterpret_cast<char *>(readBuffer.data()), writeBytes);
    totalBytesWritten += writeBytes;
  }

  // pdf specific
  if (!foundEnd && formatIndex == 2 && xrefFound && trailerFound) {
    foundEnd = true;  // PDF files can be considered valid if they have xref and
                      // trailer
    // PDF specific
    outFile.write(reinterpret_cast<char *>(END_MARKERS[formatIndex].data()),
                  END_MARKERS[formatIndex].size());
  }

  outFile.close();
  file.close();

  size_t minSize = Size_limit[formatIndex].first;   // Convert to bytes
  size_t maxSize = Size_limit[formatIndex].second;  // Convert to bytes

  if (foundEnd &&
      (totalBytesWritten < minSize || totalBytesWritten > maxSize)) {
    cout << "[SKIP] Deleted invalid size file: " << outFileName << " ("
         << totalBytesWritten << " bytes)\n";
    remove(outFileName.c_str());  // Delete file
    fileCount--;                  // Decrement file count to avoid gaps
    return;
  }
  if (!foundEnd || (formatIndex == 2 && !xrefFound && !trailerFound)) {
    cout << "[SKIP] Deleted file without end marker: " << outFileName << " ("
         << totalBytesWritten << " bytes)\n";
    remove(outFileName.c_str());  // Delete file
    fileCount--;                  // Decrement file count to avoid gaps
    return;
  } else {
    cout << "[OK] Recovered: " << outFileName << " ("
         << totalBytesWritten / 1024 << " KB)\n";
    return;
  }
}

int main() {
  const string filename = "/dev/sda";
  const size_t CHUNK_SIZE = 4096;
  ifstream file(filename, ios::binary);
  if (!file) {
    cerr << "Failed to open " << filename << endl;
    return 1;
  }
  Mp3 mp3;
  MP4 mp4;
  int fileCount = 0;
  size_t offset = 0;
  size_t mp3_offset_done = 0;  // Counter for MP3 files
  size_t overlap = 0;
  vector<unsigned char> buffer(CHUNK_SIZE + overlap);

  while (file.read(reinterpret_cast<char *>(buffer.data() + overlap),
                   CHUNK_SIZE) ||
         file.gcount() > 0) {
    size_t bytesRead = file.gcount();

    for (int formatIndex = 0; formatIndex < SupportedFileCount; formatIndex++) {
      for (size_t i = 0; i + SIGNATURES[i].size() <= bytesRead + overlap; i++) {
        size_t fileStart = offset + i;
        if ((formatIndex == 4 && mp3.matchesMP3Header(buffer, i) &&
             offset + i >= mp3_offset_done)) {
          cout << "found mp3 header at offset: " << fileStart << endl;
          mp3_offset_done =
              mp3.extractMP3File(filename, fileStart, ++fileCount);
          i += 4;
        } else if (formatIndex == 7 &&
                   mp4.matchesMP4Header(buffer, MP4_SIGNATURE, i)) {
          cout << "Found MP4 header at offset: " << fileStart << endl;
          mp4.extractMP4File(filename, fileStart, ++fileCount);
          i += 8;  // Skip the 'ftyp' header
        } else if ((formatIndex != 4 && formatIndex != 7 &&
                    matchesSignature(buffer, i, SIGNATURES[formatIndex]))) {
          cout << "Found file signature at offset: " << fileStart << endl;
          extractFile(filename, fileStart, ++fileCount, formatIndex);
          i += SIGNATURES[formatIndex].size();  // Skip some bytes to avoid
                                                // detecting same file again
        }
      }
    }
    offset += bytesRead;
    if (bytesRead == CHUNK_SIZE) {
      for (int i = 0; i < overlap; i++) {
        buffer[i] = buffer[CHUNK_SIZE + i -
                           overlap];  // Shift overlap bytes to the front
      }
    }
  }

  file.close();
  return 0;
}