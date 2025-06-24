#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <algorithm> // For std::min
#include <iomanip>   // For std::hex, std::setw, std::setfill for debug prints
#include <cerrno>    // For errno

// For creating directories (platform-dependent)
#ifdef _WIN32
#include <direct.h> // For _mkdir
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>                 // For mkdir
#define MKDIR(path) mkdir(path, 0777) // 0777 for read/write/execute for everyone
#endif

using namespace std;

class MP4
{
    // Signatures for MP4 boxes. The first 4 bytes are size, next 4 are type.
    // We only compare the type part (bytes 4-7).
    vector<unsigned char> FYTP_SIGNATURE = {0x00, 0x00, 0x00, 0x00, 0x66, 0x79, 0x70, 0x74}; // ftyp
    vector<unsigned char> MOOV_SIGNATURE = {0x00, 0x00, 0x00, 0x00, 0x6D, 0x6F, 0x6F, 0x76}; // moov
    vector<unsigned char> MDAT_SIGNATURE = {0x00, 0x00, 0x00, 0x00, 0x6D, 0x64, 0x61, 0x74}; // mdat

public:
    // Public access to signatures for external scanning if needed (e.g., in a main function)
    const vector<unsigned char> &getFtypSignature() const { return FYTP_SIGNATURE; }
    const vector<unsigned char> &getMoovSignature() const { return MOOV_SIGNATURE; }
    const vector<unsigned char> &getMdatSignature() const { return MDAT_SIGNATURE; }

    // Checks if the buffer at a given offset matches an MP4 box signature.
    // Compares bytes from index 4 onwards (skipping the size field).
    bool matchesMP4Header(const vector<unsigned char> &buffer, const vector<unsigned char> &signature, size_t offset)
    {
        // Ensure there are enough bytes in the buffer to compare the full signature
        if (offset + signature.size() > buffer.size())
            return false;
        // Compare the 4-byte type signature (bytes 4 to 7 of the 8-byte header)
        for (size_t i = 4; i < signature.size(); ++i)
        {
            if (buffer[offset + i] != signature[i])
                return false;
        }
        return true;
    }

    // Appends the content of one binary file to another.
    bool appendFile(const string &fileName, const string &otherFileName)
    {
        ifstream srcFile(otherFileName, ios::binary);
        if (!srcFile)
        {
            cerr << "Failed to open source file for appending: " << otherFileName << endl;
            return false;
        }

        // Open destination file in append mode
        ofstream destFile(fileName, ios::binary | ios::app);
        if (!destFile)
        {
            cerr << "Failed to open destination file for appending: " << fileName << endl;
            return false;
        }

        // Copy the entire content from source to destination
        destFile << srcFile.rdbuf();

        // Check for write errors
        if (!destFile)
        {
            cerr << "Failed during write to file: " << fileName << endl;
            return false;
        }
        srcFile.close();
        destFile.close();
        return true;
    }

    // Extracts and recovers an MP4 file from a larger binary stream.
    void extractMP4File(const string &filename, size_t startOffset, int fileCount)
    {
        ifstream file(filename, ios::binary);
        if (!file)
        {
            cerr << "Failed to open input file: " << filename << endl;
            return;
        }

        // Create the output directory if it doesn't exist
        string outputDir = "./RecoveredData/MP4";
        // Check if directory creation was successful or if it already exists
        if (MKDIR(outputDir.c_str()) != 0 && errno != EEXIST)
        {
            cerr << "Failed to create directory: " << outputDir << endl;
            file.close();
            return;
        }

        // Use a fixed chunk size for reading the input file
        const size_t CHUNK_SIZE = 1024 * 1024; // 1 MB
        // Overlap for detecting headers split across buffer boundaries (4 bytes size + 4 bytes type = 8, so overlap 7)
        const int OVERLAP_SIZE = 7;

        // Buffer to hold file chunks, with extra space for overlap
        vector<unsigned char> buffer(CHUNK_SIZE + OVERLAP_SIZE);

        // Seek to the starting offset in the input file
        file.seekg(startOffset, ios::beg);

        bool foundFytp = false;
        bool foundMoov = false;
        bool foundMdat = false;

        string outFileName = outputDir + "/RecoveredFile_" + to_string(fileCount) + ".mp4";
        string outMOOVName = outputDir + "/Temp__moov.mp4";
        string outMDATName = outputDir + "/Temp__mdat.mp4";

        // Open output files. Crucial to check if they opened successfully.
        ofstream outFileMOOV(outMOOVName, ios::binary);
        if (!outFileMOOV)
        {
            cerr << "Failed to create temporary MOOV file: " << outMOOVName << endl;
            file.close();
            return;
        }
        ofstream outFileMDAT(outMDATName, ios::binary);
        if (!outFileMDAT)
        {
            cerr << "Failed to create temporary MDAT file: " << outMDATName << endl;
            outFileMOOV.close();
            file.close();
            return;
        }
        ofstream outFile(outFileName, ios::binary);
        if (!outFile)
        {
            cerr << "Failed to create output file: " << outFileName << endl;
            outFileMOOV.close();
            outFileMDAT.close();
            file.close();
            return;
        }

        // --- Helper lambda for writing and reading remaining box data ---
        // This avoids code duplication for ftyp, moov, mdat and handles spanning chunks.
        // It takes the output stream, the total box size, and the current index 'i' in the main buffer
        // (passed by reference so it can be updated if the box fits entirely within the buffer).
        auto writeBoxData = [&](ofstream &outStream, size_t boxSize, size_t &currentBufferIdx, size_t currentBufferDataSize)
        {
            // Calculate how many bytes of this box can be read from the current buffer (from currentBufferIdx)
            size_t bytesAvailableInCurrentBuffer = currentBufferDataSize - currentBufferIdx;
            size_t bytesToReadFromBuffer = min(boxSize, bytesAvailableInCurrentBuffer);

            // Write the portion of the box that is available in the current buffer
            outStream.write(reinterpret_cast<const char *>(buffer.data() + currentBufferIdx), bytesToReadFromBuffer);

            // If the entire box was in this buffer read
            if (boxSize <= bytesAvailableInCurrentBuffer)
            {
                // Entire box fits in current buffer. Move currentBufferIdx past the box.
                currentBufferIdx += boxSize - 1; // -1 because the main for loop will increment 'i' by 1
                return true;                     // Box fully written
            }
            else
            {
                // Box spans multiple buffer reads - **ROBUST HANDLING**
                size_t bytesRemainingInBox = boxSize - bytesToReadFromBuffer;
                vector<char> tempReadBuffer(CHUNK_SIZE); // Use CHUNK_SIZE for reading remaining parts sequentially

                while (bytesRemainingInBox > 0)
                {
                    size_t chunkToRead = min(bytesRemainingInBox, tempReadBuffer.size());
                    file.read(tempReadBuffer.data(), chunkToRead); // Read directly into temp buffer from current file position
                    size_t currentChunkRead = file.gcount();

                    if (currentChunkRead == 0)
                    {
                        cerr << "Warning: Reached end of input file while reading full box. Box might be truncated." << endl;
                        return false; // Incomplete box
                    }
                    outStream.write(tempReadBuffer.data(), currentChunkRead);
                    bytesRemainingInBox -= currentChunkRead;
                }
                // After fully reading the box, the main 'for' loop should break
                // and the outer 'while' loop will perform the next `file.read` from the correct position.
                return true; // Box fully written
            }
        };

        // Main loop for reading the input file in chunks
        while (true)
        {
            // Move the OVERLAP_SIZE bytes from the end of the previous buffer read to the beginning of the current
            // This ensures headers split across buffer boundaries are detected.
            for (int i = 0; i < OVERLAP_SIZE; ++i)
            {
                // Defensive check to prevent out-of-bounds access if buffer was not fully filled in previous read
                if (buffer.size() - OVERLAP_SIZE + i < buffer.size())
                    buffer[i] = buffer[buffer.size() - OVERLAP_SIZE + i];
            }

            // Read new data into the buffer, starting after the overlap bytes
            file.read(reinterpret_cast<char *>(buffer.data() + OVERLAP_SIZE), CHUNK_SIZE);
            size_t bytesRead = file.gcount(); // Number of bytes actually read in this operation

            // If no bytes were read, we've reached the end of the input file
            if (bytesRead == 0)
                break;

            // `currentBufferDataSize` is the total amount of valid data in the buffer for this iteration,
            // including the overlapped part.
            size_t currentBufferDataSize = bytesRead + OVERLAP_SIZE;

            // Iterate through the buffer to find signatures
            for (size_t i = 0; i < currentBufferDataSize; ++i)
            {
                // Ensure there are at least 8 bytes available for a box header (size + type)
                if (i + 8 > currentBufferDataSize)
                {
                    // Not enough data for a full header, so break from this inner loop.
                    // Remaining data will be handled by overlap in the next outer loop iteration.
                    break;
                }

                // --- FYTP BOX ---
                if (!foundFytp && matchesMP4Header(buffer, FYTP_SIGNATURE, i))
                {
                    size_t fytpSize = (static_cast<size_t>(buffer[i]) << 24) |
                                      (static_cast<size_t>(buffer[i + 1]) << 16) |
                                      (static_cast<size_t>(buffer[i + 2]) << 8) |
                                      static_cast<size_t>(buffer[i + 3]);

                    // Sanity check for box size: must be at least 8 bytes (header)
                    // and not excessively large (e.g., larger than 200MB, typical ftyp is very small)
                    if (fytpSize < 8 || fytpSize > 200 * 1024 * 1024)
                    {
                        // Potentially a false positive or corrupted header, skip it.
                        // Can add cerr here for debug if needed.
                        continue;
                    }

                    if (writeBoxData(outFile, fytpSize, i, currentBufferDataSize))
                    {
                        foundFytp = true;
                        // If the box spanned, writeBoxData read the rest and advanced file pointer.
                        // We need to break the inner loop to let the outer loop read the next chunk.
                        // The 'i' is updated by writeBoxData only if it fit in current chunk.
                        // If it spanned, 'i' will not be updated correctly for the current buffer's context,
                        // so we MUST break.
                        break;
                    }
                }
                // --- MOOV BOX ---
                else if (!foundMoov && matchesMP4Header(buffer, MOOV_SIGNATURE, i))
                {
                    size_t moovSize = (static_cast<size_t>(buffer[i]) << 24) |
                                      (static_cast<size_t>(buffer[i + 1]) << 16) |
                                      (static_cast<size_t>(buffer[i + 2]) << 8) |
                                      static_cast<size_t>(buffer[i + 3]);

                    if (moovSize < 8 || moovSize > 200 * 1024 * 1024)
                    {
                        continue;
                    }

                    if (writeBoxData(outFileMOOV, moovSize, i, currentBufferDataSize))
                    {
                        foundMoov = true;
                        // Same logic as ftyp: if it spanned, break inner for loop.
                        break;
                    }
                }
                // --- MDAT BOX ---
                else if (!foundMdat && matchesMP4Header(buffer, MDAT_SIGNATURE, i))
                {
                    size_t mdatSize = (static_cast<size_t>(buffer[i]) << 24) |
                                      (static_cast<size_t>(buffer[i + 1]) << 16) |
                                      (static_cast<size_t>(buffer[i + 2]) << 8) |
                                      static_cast<size_t>(buffer[i + 3]);

                    // MDAT can be extremely large, so we don't put an upper limit like for ftyp/moov.
                    if (mdatSize < 8)
                    { // Minimum MDAT size
                        continue;
                    }

                    if (writeBoxData(outFileMDAT, mdatSize, i, currentBufferDataSize))
                    {
                        foundMdat = true;
                        // MDAT is typically the last major box. If it's fully written,
                        // we can break the inner loop and then the outer loop.
                        break;
                    }
                }
            } // End of inner 'for' loop

            // If we've found and fully processed all essential boxes, we can stop reading the input file.
            if (foundFytp && foundMoov && foundMdat)
            {
                break; // Break the outer 'while' loop
            }
        } // End of outer 'while' loop

        // Close temporary files and the main output file before appending
        outFileMOOV.close();
        outFileMDAT.close();
        outFile.close();

        bool recoverySuccess = true;
        if (foundFytp)
        {
            // Append MOOV if found
            if (foundMoov && !appendFile(outFileName, outMOOVName))
            {
                cerr << "[ERROR] Failed to append MOOV data to " << outFileName << endl;
                recoverySuccess = false;
            }
            // Append MDAT if found
            if (foundMdat && !appendFile(outFileName, outMDATName))
            {
                cerr << "[ERROR] Failed to append MDAT data to " << outFileName << endl;
                recoverySuccess = false;
            }
        }
        else
        {
            cerr << "[ERROR] Failed to find 'ftyp' box. Cannot recover MP4." << endl;
            recoverySuccess = false;
        }

        if (recoverySuccess)
        {
            // Reopen the final file to get its actual size after all appends
            ifstream finalFileCheck(outFileName, ios::binary | ios::ate);
            if (finalFileCheck)
            {
                size_t finalFileSize = finalFileCheck.tellg();
                cout << "[OK] Recovered: " << outFileName
                     << " (Actual Size: " << finalFileSize / 1024 << " KB)\n";
            }
            else
            {
                cout << "[OK] Recovered: " << outFileName
                     << " (Actual Size: Unknown, but recovery attempted)\n";
            }
        }
        else
        {
            cerr << "[ERROR] MP4 file recovery failed or was incomplete for: " << outFileName << endl;
            // If recovery failed, delete the incomplete output file
            remove(outFileName.c_str());
        }

        file.close(); // Close the input file
        // Clean up temporary files
        remove(outMOOVName.c_str());
        remove(outMDATName.c_str());
    }
};