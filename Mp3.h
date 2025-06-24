
#include <sys/stat.h>

#include <QString>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <vector>
namespace fs = std::filesystem;
using namespace std;

class Mp3
{
public:
  // --- MP3 Constants ---
  const size_t MAX_GAP_Bytes =
      0.75 * 1024; // bytes, maximum gap between frames
  const vector<int> MPEG_VERSIONS = {
      2,  // 00 = MPEG 2.5
      -1, // 01 = reserved
      2,  // 10 = MPEG 2
      1   // 11 = MPEG 1
  };

  const vector<int> LAYERS = {
      0, // 00 = reserved
      3, // 01 = Layer III
      2, // 10 = Layer II
      1  // 11 = Layer I
  };

  // match criteria for frame info
  //  matchFrameSize: true if frame size should match, false if it can vary
  bool matchFrameSize = false;
  bool matchVersion = true;
  bool matchLayer = false;
  bool matchBitrate = false;
  bool matchSamplingRate = false;

  vector<bool> matchAllowed = {matchFrameSize, matchVersion, matchLayer,
                               matchBitrate, matchSamplingRate};

  // [version][layer][bitrate_index]
  const vector<vector<vector<int>>> BITRATE_TABLE = {
      {// MPEG 1
       {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448,
        0}, // Layer I
       {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384,
        0}, // Layer II
       {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320,
        0}}, // Layer III

      {// MPEG 2/2.5
       {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256,
        0}, // Layer I
       {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,
        0}, // Layer II
       {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,
        0}} // Layer III
  };

  // [version][sampling_rate_index]
  const vector<vector<int>> SAMPLING_RATE_TABLE = {
      {11025, 12000, 8000, 0},  // MPEG 2.5
      {0, 0, 0, 0},             // reserved
      {22050, 24000, 16000, 0}, // MPEG 2
      {44100, 48000, 32000, 0}  // MPEG 1
  };

  // --- MP3 Frame Header Parser ---
  vector<int> parse_mp3_frame_header(const unsigned char *header)
  {
    vector<int> frame_info(5, 0); // Initialize frame_info with 5 zeros
    if (header[0] != 0xFF || (header[1] & 0xE0) != 0xE0)
      return frame_info; // Not a valid sync

    uint8_t version_id = (header[1] >> 3) & 0x03;
    uint8_t layer_id = (header[1] >> 1) & 0x03;
    uint8_t bitrate_index = (header[2] >> 4) & 0x0F;
    uint8_t sampling_rate_index = (header[2] >> 2) & 0x03;
    uint8_t padding_bit = (header[2] >> 1) & 0x01;

    if (version_id == 1 || layer_id == 0 || bitrate_index == 0 ||
        bitrate_index == 15 || sampling_rate_index == 3)
      return frame_info; // Invalid version/layer/index

    int mpeg_version = MPEG_VERSIONS[version_id]; // 0 = MPEG1, 1 = MPEG2
    int layer = LAYERS[layer_id];

    if (mpeg_version == -1 || layer == 0)
      return frame_info;

    int bitrate =
        BITRATE_TABLE[mpeg_version == 1 ? 0 : 1][layer - 1][bitrate_index] *
        1000;
    int sampling_rate = SAMPLING_RATE_TABLE[version_id][sampling_rate_index];

    if (bitrate == 0 || sampling_rate == 0)
      return frame_info;

    int frame_size = 0;
    if (layer == 1) // Layer I
      frame_size =
          (unsigned int)(12 * bitrate / sampling_rate + padding_bit) * 4;
    else // Layer II & III
      frame_size = (unsigned int)144 * bitrate / sampling_rate + padding_bit;

    frame_info[0] = frame_size;    // Frame size
    frame_info[1] = mpeg_version;  // MPEG version
    frame_info[2] = layer;         // Layer
    frame_info[3] = bitrate;       // Bitrate
    frame_info[4] = sampling_rate; // Sampling rate
    return frame_info;
  }
  bool matchesMP3Header(const vector<unsigned char> &buffer, size_t pos)
  {
    if (pos + 1 >= buffer.size())
      return false;

    vector<int> frame_info = parse_mp3_frame_header(&buffer[pos]);
    if (frame_info[0] <= 0)
      return false;
    for (int i = 1; i <= 10; ++i)
    {
      bool found = false;
      size_t gap_count = 0;
      while (gap_count < MAX_GAP_Bytes &&
             pos + frame_info[0] * i + gap_count < buffer.size())
      {
        frame_info = parse_mp3_frame_header(
            &buffer[pos + frame_info[0] * i + gap_count]);
        if (frame_info[0] > 0)
        {
          // return true; // Found a valid MP3 frame header
          found = true;
          gap_count = 0; // reset gap count if we found a valid frame
          break;
        }
        gap_count++;
      }
      if (!found && gap_count >= MAX_GAP_Bytes)
      {
        return false;
      }
    }
    return true;
  }

  bool matchesFrameInfo(const vector<int> &frame,
                        const vector<int> &frame_info_original)
  {
    // cout << "checkpoint 1.8" << endl;
    // cout << "frame size: " << frame[0] << endl;
    if (frame_info_original.empty() || frame.empty())
    {
      // cout << "frame_info_original is empty" << endl;
      return false;
    }
    // cout << "original frame size: " << frame_info_original[0] << endl;
    if (frame[0] <= 0 || frame_info_original[0] <= 0 || frame.size() != 5 ||
        frame_info_original.size() != 5)
      return false;
    // cout << "checkpoint 1.9" << endl;
    if (matchFrameSize && frame[0] != frame_info_original[0])
      return false;
    // cout << "checkpoint 1.95" << endl;
    if (matchVersion && frame[1] != frame_info_original[1])
      return false;
    // cout << "checkpoint 1.96" << endl;
    if (matchLayer && frame[2] != frame_info_original[2])
      return false;
    // cout << "checkpoint 1.97" << endl;
    if (matchBitrate && frame[3] != frame_info_original[3])
      return false;
    // cout << "checkpoint 1.98" << endl;
    if (matchSamplingRate && frame[4] != frame_info_original[4])
      return false;
    // cout << "checkpoint 1.99" << endl;

    // If all checks passed, the frame matches the original frame information
    return true;
  }

  size_t extractMP3File(const string &filename, size_t fileStart,
                        int &fileCount, function<void(QString)> logCallback,
                        function<bool()> cancelCheck)
  {
    ifstream file(filename, ios::binary);
    size_t current_offset = fileStart; // track the absolute byte offset
    if (!file)
    {
      cout << "[MP3] Error: Failed to open file for MP3 extraction: "
           << filename << endl;
      logCallback("Failed to reopen file for MP3 extraction");
      return current_offset;
    }
    file.clear();
    file.seekg(fileStart, ios::beg);

    // Ensure directory exists
    fs::create_directories(outputDirectory + "/MP3");

    string outFileName = outputDirectory + "/MP3/recoveredFile_" +
                         to_string(fileCount) + ".mp3";
    ofstream outFile(outFileName, ios::binary);
    if (!outFile)
    {
      logCallback("Error: Failed to create MP3 output file: " +
                  QString::fromStdString(outFileName));
      return current_offset;
    }
    // cout << "[MP3] Extracting file: " << outFileName << endl;
    const size_t BUFFER_SIZE = 4096;
    size_t overlap = 3; // carry last 3 bytes forward

    vector<unsigned char> buffer(
        BUFFER_SIZE + overlap +
        4); // +4 to avoid out-of-bounds for header read

    size_t totalExtracted = 0;
    int gapCount = 0;

    vector<int> frame_info_original;
    bool firstFrameFound = false;
    size_t totalBytesWritten = 0;
    // cout << "[MP3] Starting extraction from offset: " << fileStart << endl; // mark
    while (true)
    {
      //   if (cancelCheck()) {
      //     logCallback("Extraction cancelled");
      //     break;
      //   }
      // Move last `overlap` bytes to the front
      for (size_t i = 0; i < overlap; ++i)
      {
        buffer[i] = buffer[BUFFER_SIZE + i - overlap];
      }

      // Read new data after the carried-forward bytes
      file.read(reinterpret_cast<char *>(buffer.data() + overlap), BUFFER_SIZE);
      size_t bytesRead = file.gcount();
      if (bytesRead == 0)
        break;
      // cout << "checkpoint 1" << endl;
      size_t totalBytes = bytesRead + overlap;
      size_t pos = 0;

      while (pos + 4 <= totalBytes)
      {

        // cout << "checkout 1.5" << endl;
        vector<int> frame_info = parse_mp3_frame_header(&buffer[pos]);
        if (!firstFrameFound && frame_info[0] > 0 &&
            matchesMP3Header(buffer, pos))
        {
          frame_info_original = frame_info;
          firstFrameFound = true;
        }
        // cout << "checkout 1.6" << endl;
        // cout << frame_info[0] << endl;
        if ((matchesFrameInfo(frame_info, frame_info_original) &&
             frame_info[0] > 0 && pos + frame_info[0] <= totalBytes))
        {
          // cout << "checkout 1.6.5" << endl;
          outFile.write(reinterpret_cast<char *>(buffer.data() + pos),
                        frame_info[0]);
          current_offset += frame_info[0];
          pos += frame_info[0];
          totalExtracted += frame_info[0];
          gapCount = 0;
          totalBytesWritten += frame_info[0];
          // cout << "checkout 1.6.6" << endl;
        }
        else
        {
          // cout << "checkout 1.6.7" << endl;
          gapCount++;
          if (gapCount > MAX_GAP_Bytes)
            goto extraction_finished;

          current_offset++;
          pos++;
        }
        // cout << "checkout 1.7" << endl;
        if (totalExtracted > 50 * 1024 * 1024)
          goto extraction_finished;
      }

      // cout << "checkpoint 2" << endl;
    }

  extraction_finished:
    outFile.close();
    size_t minSize = 20 * 1024;        // Convert to bytes
    size_t maxSize = 20 * 1024 * 1024; // Convert to bytes

    // cout << "[MP3] Extraction finished. Total bytes written: "
    //  << totalBytesWritten << endl;
    if (totalBytesWritten < minSize || totalBytesWritten > maxSize)
    {
      // logCallback("[SKIP] Deleted invalid size Mp3 file: (" +
      //             QString::number(totalBytesWritten) + " bytes)");
      remove(outFileName.c_str()); // Delete file
      fileCount--;                 // Decrement file count to avoid gaps
      return 0;
    }
    else
    {
      logCallback("[OK] Recovered: " + QString::fromStdString(outFileName) +
                  " (" + QString::number(totalBytesWritten / 1024) + " KB)");
    }
    return current_offset;
  }
  string outputDirectory;
  Mp3(const string &outputDir) : outputDirectory(outputDir)
  {
    // Constructor can initialize logging and progress callbacks if needed
  }
};