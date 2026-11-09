#include <iostream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#endif

#pragma pack(push, 1)

// MBR Partition Record (16 bytes)
struct MBRPartitionRecord {
    uint8_t  bootIndicator;
    uint8_t  startHead;
    uint8_t  startSector;
    uint8_t  startCylinder;
    uint8_t  partitionType;
    uint8_t  endHead;
    uint8_t  endSector;
    uint8_t  endCylinder;
    uint32_t startingLBA;
    uint32_t sectorCount;
};

// MBR Boot Sector (512 bytes)
struct MBRBootSector {
    uint8_t               bootstrapCode[446];
    MBRPartitionRecord    partitions[4];
    uint16_t              bootSignature; // 0xAA55
};

// GPT Header Structure (subset of LBA 1)
struct GPTHeader {
    char     signature[8];       // "EFI PART"
    uint32_t revision;
    uint32_t headerSize;
    uint32_t headerCRC32;
    uint32_t reserved;
    uint64_t currentLBA;
    uint64_t backupLBA;
    uint64_t firstUsableLBA;
    uint64_t lastUsableLBA;
    uint8_t  diskGUID[16];
    uint64_t partitionEntryLBA;  // Usually LBA 2
    uint32_t numPartitionEntries;
    uint32_t sizeOfPartitionEntry; // Usually 128 bytes
    uint32_t partitionEntryArrayCRC32;
};

// GPT Partition Entry Structure (128 bytes)
struct GPTPartitionEntry {
    uint8_t  typeGUID[16];
    uint8_t  uniqueGUID[16];
    uint64_t startingLBA;
    uint64_t endingLBA;
    uint64_t attributes;
    uint16_t partitionName[36]; // UTF-16LE
};

#pragma pack(pop)

const uint32_t MAX_MBR_LBA = 0xFFFFFFFF; // 32-bit sector limit (~2.19 TB)
const uint32_t SECTOR_SIZE = 512;

void PrintSafetyHeader() {
    std::cout << "=================================================================\n";
    std::cout << " [!] GPT-TO-MBR CONVERSION - BACK UP FIRST [!]\n";
    std::cout << "=================================================================\n";
    std::cout << " The conversion writes only partition-table metadata sectors;\n";
    std::cout << " it does not move or format partition contents. It cannot guarantee\n";
    std::cout << " bootability or recovery from power loss, hardware failure, or bugs.\n";
    std::cout << " A verified full-disk backup is strongly recommended.\n\n";
    std::cout << " GPT GUIDs, names, attributes, and boot configuration do not all\n";
    std::cout << " have MBR equivalents. Unsupported layouts and partition types\n";
    std::cout << " are rejected. Successful conversion may still leave the disk\n";
    std::cout << " unbootable or incompatible with the installed operating system.\n";
    std::cout << "=================================================================\n\n";
}

#ifdef _WIN32

struct GPTCopy {
    GPTHeader header{};
    std::vector<uint8_t> headerSector;
    std::vector<uint8_t> entries;
    uint64_t entriesSectors = 0;
    uint64_t entriesBytes = 0;
};

#pragma pack(push, 1)
struct GPTBackupFileHeader {
    char magic[8];
    uint32_t version;
    uint32_t sectorSize;
    uint64_t diskSectors;
    uint64_t primaryEntriesLBA;
    uint64_t backupEntriesLBA;
    uint64_t entriesSectors;
    uint64_t entriesBytes;
    uint32_t payloadCRC32;
};
#pragma pack(pop)

bool ReadAt(HANDLE hDisk, uint64_t offset, uint8_t* buffer, size_t byteCount) {
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(hDisk, li, NULL, FILE_BEGIN)) {
        return false;
    }

    size_t completed = 0;
    while (completed < byteCount) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(byteCount - completed, 1U << 20));
        DWORD bytesRead = 0;
        if (!ReadFile(hDisk, buffer + completed, chunk, &bytesRead, NULL) || bytesRead != chunk) {
            return false;
        }
        completed += bytesRead;
    }
    return true;
}

bool WriteAt(HANDLE hDisk, uint64_t offset, const uint8_t* buffer, size_t byteCount) {
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(hDisk, li, NULL, FILE_BEGIN)) {
        return false;
    }

    size_t completed = 0;
    while (completed < byteCount) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(byteCount - completed, 1U << 20));
        DWORD bytesWritten = 0;
        if (!WriteFile(hDisk, buffer + completed, chunk, &bytesWritten, NULL) || bytesWritten != chunk) {
            return false;
        }
        completed += bytesWritten;
    }
    return true;
}

bool ReadLBA(HANDLE hDisk, uint64_t lba, uint8_t* buffer, size_t byteCount) {
    if (lba > UINT64_MAX / SECTOR_SIZE) {
        return false;
    }
    return ReadAt(hDisk, lba * SECTOR_SIZE, buffer, byteCount);
}

bool WriteLBA(HANDLE hDisk, uint64_t lba, const uint8_t* buffer, size_t byteCount) {
    if (lba > UINT64_MAX / SECTOR_SIZE) {
        return false;
    }
    return WriteAt(hDisk, lba * SECTOR_SIZE, buffer, byteCount);
}

bool VerifyLBA(HANDLE hDisk, uint64_t lba, const uint8_t* expected, size_t byteCount) {
    std::vector<uint8_t> check(std::min<size_t>(byteCount, 1U << 20));
    size_t completed = 0;
    while (completed < byteCount) {
        size_t chunk = std::min(check.size(), byteCount - completed);
        if (!ReadLBA(hDisk, lba + completed / SECTOR_SIZE, check.data(), chunk) ||
            std::memcmp(check.data(), expected + completed, chunk) != 0) {
            return false;
        }
        completed += chunk;
    }
    return true;
}

uint32_t CalculateCRC32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

bool IsAllZero(const uint8_t* data, size_t size) {
    return std::all_of(data, data + size, [](uint8_t value) { return value == 0; });
}

bool RangesOverlap(uint64_t firstStart, uint64_t firstCount,
                   uint64_t secondStart, uint64_t secondCount) {
    return firstStart <= secondStart + secondCount - 1 &&
           secondStart <= firstStart + firstCount - 1;
}

bool LoadGPTCopy(HANDLE hDisk, uint64_t diskSectors, uint64_t headerLBA,
                 GPTCopy& copy, std::string& error) {
    copy.headerSector.resize(SECTOR_SIZE);
    if (!ReadLBA(hDisk, headerLBA, copy.headerSector.data(), copy.headerSector.size())) {
        error = "Failed to read GPT header at LBA " + std::to_string(headerLBA) + ".";
        return false;
    }

    if (std::memcmp(copy.headerSector.data(), "EFI PART", 8) != 0) {
        error = "GPT signature is missing at LBA " + std::to_string(headerLBA) + ".";
        return false;
    }

    std::memcpy(&copy.header, copy.headerSector.data(), sizeof(copy.header));
    const GPTHeader& header = copy.header;
    if (header.revision != 0x00010000 || header.headerSize < sizeof(GPTHeader) ||
        header.headerSize > SECTOR_SIZE || header.reserved != 0 ||
        header.currentLBA != headerLBA || header.backupLBA >= diskSectors ||
        header.firstUsableLBA > header.lastUsableLBA ||
        header.lastUsableLBA >= diskSectors || header.numPartitionEntries == 0 ||
        header.sizeOfPartitionEntry < sizeof(GPTPartitionEntry) ||
        header.sizeOfPartitionEntry > 4096 || header.sizeOfPartitionEntry % 8 != 0) {
        error = "GPT header fields are invalid or unsupported at LBA " +
                std::to_string(headerLBA) + ".";
        return false;
    }

    std::vector<uint8_t> crcData(copy.headerSector.begin(),
                                 copy.headerSector.begin() + header.headerSize);
    std::memset(crcData.data() + offsetof(GPTHeader, headerCRC32), 0, sizeof(header.headerCRC32));
    if (CalculateCRC32(crcData.data(), crcData.size()) != header.headerCRC32) {
        error = "GPT header CRC does not match at LBA " + std::to_string(headerLBA) + ".";
        return false;
    }

    copy.entriesBytes = static_cast<uint64_t>(header.numPartitionEntries) *
                        header.sizeOfPartitionEntry;
    if (copy.entriesBytes > (1ULL << 26)) {
        error = "GPT partition-entry array exceeds the supported 64 MiB limit.";
        return false;
    }
    copy.entriesSectors = (copy.entriesBytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (header.partitionEntryLBA >= diskSectors ||
        copy.entriesSectors > diskSectors - header.partitionEntryLBA) {
        error = "GPT partition-entry array is outside the disk.";
        return false;
    }

    copy.entries.resize(static_cast<size_t>(copy.entriesSectors * SECTOR_SIZE));
    if (!ReadLBA(hDisk, header.partitionEntryLBA, copy.entries.data(), copy.entries.size())) {
        error = "Failed to read GPT partition-entry array.";
        return false;
    }
    if (CalculateCRC32(copy.entries.data(), static_cast<size_t>(copy.entriesBytes)) !=
        header.partitionEntryArrayCRC32) {
        error = "GPT partition-entry array CRC does not match.";
        return false;
    }
    return true;
}

bool MapPartitionType(const uint8_t typeGUID[16], uint8_t& mbrType) {
    static const uint8_t basicData[16] =
        {0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7};
    static const uint8_t efiSystem[16] =
        {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};
    static const uint8_t windowsRecovery[16] =
        {0xA4, 0xBB, 0x94, 0xDE, 0xD1, 0x06, 0x40, 0x4D, 0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC};
    static const uint8_t linuxFilesystem[16] =
        {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4};
    static const uint8_t linuxSwap[16] =
        {0x6D, 0xFD, 0x57, 0x06, 0xAB, 0xA4, 0xC4, 0x43, 0x84, 0xE5, 0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F};

    if (std::memcmp(typeGUID, basicData, 16) == 0) mbrType = 0x07;
    else if (std::memcmp(typeGUID, efiSystem, 16) == 0) mbrType = 0xEF;
    else if (std::memcmp(typeGUID, windowsRecovery, 16) == 0) mbrType = 0x27;
    else if (std::memcmp(typeGUID, linuxFilesystem, 16) == 0) mbrType = 0x83;
    else if (std::memcmp(typeGUID, linuxSwap, 16) == 0) mbrType = 0x82;
    else return false;
    return true;
}

void AppendBytes(std::vector<uint8_t>& destination, const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    destination.insert(destination.end(), bytes, bytes + size);
}

bool CreateAndVerifyBackup(const std::string& path, const std::vector<uint8_t>& backup) {
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        std::cerr << "[-] Error: Cannot create backup file '" << path
                  << "'. Choose a new path on a different physical disk. Error: "
                  << GetLastError() << "\n";
        return false;
    }

    bool writeSucceeded = true;
    size_t writtenTotal = 0;
    while (writtenTotal < backup.size()) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(backup.size() - writtenTotal, 1U << 20));
        DWORD bytesWritten = 0;
        if (!WriteFile(file, backup.data() + writtenTotal, chunk, &bytesWritten, NULL) ||
            bytesWritten != chunk) {
            writeSucceeded = false;
            break;
        }
        writtenTotal += bytesWritten;
    }
    if (writeSucceeded && !FlushFileBuffers(file)) {
        writeSucceeded = false;
    }
    CloseHandle(file);

    if (writeSucceeded) {
        file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) {
            writeSucceeded = false;
        } else {
            std::vector<uint8_t> check(backup.size());
            size_t readTotal = 0;
            while (readTotal < check.size()) {
                DWORD chunk = static_cast<DWORD>(std::min<size_t>(check.size() - readTotal, 1U << 20));
                DWORD bytesRead = 0;
                if (!ReadFile(file, check.data() + readTotal, chunk, &bytesRead, NULL) ||
                    bytesRead != chunk) {
                    writeSucceeded = false;
                    break;
                }
                readTotal += bytesRead;
            }
            if (writeSucceeded && check != backup) {
                writeSucceeded = false;
            }
            CloseHandle(file);
        }
    }

    if (!writeSucceeded) {
        std::cerr << "[-] Error: Backup could not be fully written and verified; disk was not modified.\n";
        DeleteFileA(path.c_str());
        return false;
    }
    return true;
}

bool RestoreMetadata(HANDLE hDisk, uint64_t primaryHeaderLBA, uint64_t backupHeaderLBA,
                     const GPTCopy& primary, const GPTCopy& backup,
                     const std::vector<uint8_t>& originalMBR) {
    bool restored =
        WriteLBA(hDisk, backup.header.partitionEntryLBA, backup.entries.data(), backup.entries.size()) &&
        WriteLBA(hDisk, primary.header.partitionEntryLBA, primary.entries.data(), primary.entries.size()) &&
        WriteLBA(hDisk, backupHeaderLBA, backup.headerSector.data(), backup.headerSector.size()) &&
        WriteLBA(hDisk, primaryHeaderLBA, primary.headerSector.data(), primary.headerSector.size()) &&
        WriteLBA(hDisk, 0, originalMBR.data(), originalMBR.size()) &&
        FlushFileBuffers(hDisk);
    if (restored) {
        restored =
            VerifyLBA(hDisk, backup.header.partitionEntryLBA, backup.entries.data(), backup.entries.size()) &&
            VerifyLBA(hDisk, primary.header.partitionEntryLBA, primary.entries.data(), primary.entries.size()) &&
            VerifyLBA(hDisk, backupHeaderLBA, backup.headerSector.data(), backup.headerSector.size()) &&
            VerifyLBA(hDisk, primaryHeaderLBA, primary.headerSector.data(), primary.headerSector.size()) &&
            VerifyLBA(hDisk, 0, originalMBR.data(), originalMBR.size());
    }
    return restored;
}

bool RestoreFromBackupFile(const std::string& path, HANDLE hDisk, uint64_t diskSectors) {
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        std::cerr << "[-] Error: Cannot open metadata backup '" << path
                  << "'. Error: " << GetLastError() << "\n";
        return false;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize) ||
        fileSize.QuadPart < static_cast<LONGLONG>(sizeof(GPTBackupFileHeader)) ||
        fileSize.QuadPart > (1LL << 28)) {
        std::cerr << "[-] Error: Metadata backup size is invalid or exceeds 256 MiB.\n";
        CloseHandle(file);
        return false;
    }
    std::vector<uint8_t> data(static_cast<size_t>(fileSize.QuadPart));
    size_t readTotal = 0;
    while (readTotal < data.size()) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(data.size() - readTotal, 1U << 20));
        DWORD bytesRead = 0;
        if (!ReadFile(file, data.data() + readTotal, chunk, &bytesRead, NULL) ||
            bytesRead != chunk) {
            std::cerr << "[-] Error: Failed to read metadata backup.\n";
            CloseHandle(file);
            return false;
        }
        readTotal += bytesRead;
    }
    CloseHandle(file);

    GPTBackupFileHeader header{};
    std::memcpy(&header, data.data(), sizeof(header));
    if (std::memcmp(header.magic, "GPTMBR1", 7) != 0 || header.magic[7] != 0 ||
        header.version != 1 || header.sectorSize != SECTOR_SIZE ||
        header.diskSectors != diskSectors || header.entriesSectors == 0 ||
        header.entriesSectors > (1ULL << 17) ||
        header.entriesBytes == 0 ||
        header.entriesBytes > header.entriesSectors * SECTOR_SIZE ||
        header.entriesBytes <= (header.entriesSectors - 1) * SECTOR_SIZE) {
        std::cerr << "[-] Error: Metadata backup header is invalid or belongs to a different-sized disk.\n";
        return false;
    }

    const uint64_t expectedSize = sizeof(header) + 3ULL * SECTOR_SIZE +
                                  2ULL * header.entriesSectors * SECTOR_SIZE;
    if (expectedSize != data.size() ||
        CalculateCRC32(data.data() + sizeof(header), data.size() - sizeof(header)) !=
            header.payloadCRC32) {
        std::cerr << "[-] Error: Metadata backup is truncated or its payload checksum is invalid.\n";
        return false;
    }

    size_t offset = sizeof(header);
    const uint8_t* savedMBR = data.data() + offset;
    offset += SECTOR_SIZE;
    const uint8_t* primaryHeaderSector = data.data() + offset;
    offset += SECTOR_SIZE;
    const uint8_t* primaryEntries = data.data() + offset;
    offset += static_cast<size_t>(header.entriesSectors * SECTOR_SIZE);
    const uint8_t* backupEntries = data.data() + offset;
    offset += static_cast<size_t>(header.entriesSectors * SECTOR_SIZE);
    const uint8_t* backupHeaderSector = data.data() + offset;

    GPTHeader primaryHeader{};
    GPTHeader backupHeader{};
    std::memcpy(&primaryHeader, primaryHeaderSector, sizeof(primaryHeader));
    std::memcpy(&backupHeader, backupHeaderSector, sizeof(backupHeader));
    auto validSavedHeader = [&](const uint8_t* sector, const GPTHeader& gpt,
                                uint64_t expectedCurrent, uint64_t expectedBackup,
                                uint64_t expectedEntriesLBA) {
        if (std::memcmp(gpt.signature, "EFI PART", 8) != 0 ||
            gpt.revision != 0x00010000 || gpt.headerSize < sizeof(GPTHeader) ||
            gpt.headerSize > SECTOR_SIZE || gpt.currentLBA != expectedCurrent ||
            gpt.backupLBA != expectedBackup || gpt.partitionEntryLBA != expectedEntriesLBA ||
            gpt.reserved != 0 || gpt.firstUsableLBA < 2 ||
            gpt.firstUsableLBA > gpt.lastUsableLBA || gpt.lastUsableLBA >= diskSectors ||
            gpt.numPartitionEntries == 0 ||
            gpt.sizeOfPartitionEntry < sizeof(GPTPartitionEntry) ||
            gpt.sizeOfPartitionEntry > 4096 || gpt.sizeOfPartitionEntry % 8 != 0 ||
            static_cast<uint64_t>(gpt.numPartitionEntries) * gpt.sizeOfPartitionEntry != header.entriesBytes) {
            return false;
        }
        std::vector<uint8_t> crcData(sector, sector + gpt.headerSize);
        std::memset(crcData.data() + offsetof(GPTHeader, headerCRC32), 0, sizeof(gpt.headerCRC32));
        return CalculateCRC32(crcData.data(), crcData.size()) == gpt.headerCRC32;
    };

    if (static_cast<uint16_t>(savedMBR[510] | (savedMBR[511] << 8)) != 0xAA55 ||
        !validSavedHeader(primaryHeaderSector, primaryHeader, 1, diskSectors - 1,
                          header.primaryEntriesLBA) ||
        !validSavedHeader(backupHeaderSector, backupHeader, diskSectors - 1, 1,
                          header.backupEntriesLBA) ||
        primaryHeader.firstUsableLBA != backupHeader.firstUsableLBA ||
        primaryHeader.lastUsableLBA != backupHeader.lastUsableLBA ||
        primaryHeader.numPartitionEntries != backupHeader.numPartitionEntries ||
        primaryHeader.sizeOfPartitionEntry != backupHeader.sizeOfPartitionEntry ||
        std::memcmp(primaryHeader.diskGUID, backupHeader.diskGUID, sizeof(primaryHeader.diskGUID)) != 0 ||
        primaryHeader.partitionEntryArrayCRC32 != backupHeader.partitionEntryArrayCRC32 ||
        header.entriesSectors != (header.entriesBytes + SECTOR_SIZE - 1) / SECTOR_SIZE ||
        CalculateCRC32(primaryEntries, static_cast<size_t>(header.entriesBytes)) !=
            primaryHeader.partitionEntryArrayCRC32 ||
        CalculateCRC32(backupEntries, static_cast<size_t>(header.entriesBytes)) !=
            backupHeader.partitionEntryArrayCRC32 ||
        std::memcmp(primaryEntries, backupEntries,
                    static_cast<size_t>(header.entriesBytes)) != 0 ||
        header.primaryEntriesLBA < 2 ||
        header.primaryEntriesLBA >= diskSectors ||
        header.entriesSectors > diskSectors - header.primaryEntriesLBA ||
        header.primaryEntriesLBA + header.entriesSectors > primaryHeader.firstUsableLBA ||
        header.backupEntriesLBA <= primaryHeader.lastUsableLBA ||
        header.backupEntriesLBA >= diskSectors ||
        header.entriesSectors > diskSectors - header.backupEntriesLBA ||
        header.backupEntriesLBA + header.entriesSectors > diskSectors - 1 ||
        RangesOverlap(header.primaryEntriesLBA, header.entriesSectors,
                      header.backupEntriesLBA, header.entriesSectors)) {
        std::cerr << "[-] Error: Metadata backup contents or GPT layout failed validation.\n";
        return false;
    }

    std::vector<uint8_t> currentMBR(SECTOR_SIZE);
    if (!ReadLBA(hDisk, 0, currentMBR.data(), currentMBR.size()) ||
        std::memcmp(currentMBR.data(), savedMBR, 446) != 0) {
        std::cerr << "[-] Error: Target disk's preserved MBR boot-code/disk-ID region does not match the backup.\n";
        return false;
    }

    bool restored =
        WriteLBA(hDisk, header.primaryEntriesLBA, primaryEntries,
                 static_cast<size_t>(header.entriesSectors * SECTOR_SIZE)) &&
        WriteLBA(hDisk, header.backupEntriesLBA, backupEntries,
                 static_cast<size_t>(header.entriesSectors * SECTOR_SIZE)) &&
        WriteLBA(hDisk, diskSectors - 1, backupHeaderSector, SECTOR_SIZE) &&
        WriteLBA(hDisk, 1, primaryHeaderSector, SECTOR_SIZE) &&
        WriteLBA(hDisk, 0, savedMBR, SECTOR_SIZE) &&
        FlushFileBuffers(hDisk);
    if (!restored) {
        std::cerr << "[-] Error: Restoring metadata failed. Do not write to the disk again.\n";
        return false;
    }

    std::vector<uint8_t> verifyMBR(SECTOR_SIZE);
    GPTCopy verifyPrimary;
    GPTCopy verifyBackup;
    std::string verifyError;
    if (!ReadLBA(hDisk, 0, verifyMBR.data(), verifyMBR.size()) ||
        std::memcmp(verifyMBR.data(), savedMBR, verifyMBR.size()) != 0 ||
        !LoadGPTCopy(hDisk, diskSectors, 1, verifyPrimary, verifyError) ||
        !LoadGPTCopy(hDisk, diskSectors, diskSectors - 1, verifyBackup, verifyError)) {
        std::cerr << "[-] Error: Restored metadata did not pass read-back verification. "
                  << verifyError << "\n";
        return false;
    }
    std::cout << "[+] Original GPT metadata restored and GPT CRCs verified.\n";
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage:\n"
                  << "  GptToMbr.exe <PhysicalDriveNumber>                 (read-only preflight)\n"
                  << "  GptToMbr.exe <PhysicalDriveNumber> --apply --backup <external-file>\n"
                  << "  GptToMbr.exe <PhysicalDriveNumber> --restore <metadata-backup-file>\n";
        return 1;
    }

    char* end = NULL;
    long parsedDrive = std::strtol(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0' || parsedDrive < 0 || parsedDrive > 255) {
        std::cerr << "[-] Error: Physical drive number must be an integer from 0 to 255.\n";
        return 1;
    }
    const int driveNumber = static_cast<int>(parsedDrive);
    bool apply = false;
    std::string backupPath;
    std::string restorePath;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--apply") == 0) {
            apply = true;
        } else if (std::strcmp(argv[i], "--backup") == 0 && i + 1 < argc) {
            backupPath = argv[++i];
        } else if (std::strcmp(argv[i], "--restore") == 0 && i + 1 < argc) {
            restorePath = argv[++i];
        } else {
            std::cerr << "[-] Error: Unrecognized or incomplete option '" << argv[i] << "'.\n";
            return 1;
        }
    }
    if (apply && !restorePath.empty()) {
        std::cerr << "[-] Error: --apply and --restore cannot be used together.\n";
        return 1;
    }
    if (!restorePath.empty() && !backupPath.empty()) {
        std::cerr << "[-] Error: --backup and --restore cannot be used together.\n";
        return 1;
    }
    if (apply && backupPath.empty()) {
        std::cerr << "[-] Error: --apply requires --backup <external-file>.\n";
        return 1;
    }
    if (!apply && !backupPath.empty()) {
        std::cerr << "[-] Error: --backup is only used together with --apply.\n";
        return 1;
    }

    std::cout << "=== GptToMbr (Windows-Native C++ Tool) ===\n";
    std::cout << "[Target Selected]: PhysicalDrive" << driveNumber << "\n\n";
    PrintSafetyHeader();

    std::wstring devicePath = L"\\\\.\\PhysicalDrive" + std::to_wstring(driveNumber);
    const bool modifiesDisk = apply || !restorePath.empty();
    DWORD access = modifiesDisk ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
    DWORD sharing = modifiesDisk ? 0 : (FILE_SHARE_READ | FILE_SHARE_WRITE);
    HANDLE hDisk = CreateFileW(devicePath.c_str(), access, sharing, NULL, OPEN_EXISTING, 0, NULL);
    if (hDisk == INVALID_HANDLE_VALUE) {
        std::cerr << "[-] Error: Failed to open target disk. Error Code: " << GetLastError() << "\n";
        if (modifiesDisk) {
            std::cerr << "     The disk may be in use; metadata changes require exclusive access.\n";
        }
        return 1;
    }

    DISK_GEOMETRY_EX geometry{};
    DWORD bytesReturned = 0;
    GET_LENGTH_INFORMATION lengthInfo{};
    if (!DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &geometry,
                         sizeof(geometry), &bytesReturned, NULL) ||
        geometry.Geometry.BytesPerSector != SECTOR_SIZE ||
        !DeviceIoControl(hDisk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &lengthInfo,
                         sizeof(lengthInfo), &bytesReturned, NULL) ||
        lengthInfo.Length.QuadPart <= 0 ||
        lengthInfo.Length.QuadPart % SECTOR_SIZE != 0) {
        std::cerr << "[-] Error: Could not verify a 512-byte-sector disk geometry.\n";
        CloseHandle(hDisk);
        return 1;
    }
    const uint64_t diskSectors = static_cast<uint64_t>(lengthInfo.Length.QuadPart / SECTOR_SIZE);
    if (diskSectors < 68 || diskSectors > static_cast<uint64_t>(MAX_MBR_LBA) + 1) {
        std::cerr << "[-] Error: Disk size is outside the supported MBR LBA range.\n";
        CloseHandle(hDisk);
        return 1;
    }
    if (!restorePath.empty()) {
        std::cout << "[!] Restore will overwrite the disk's partition metadata with the saved GPT metadata.\n";
        std::string confirmation;
        std::cout << "Type 'RESTORE PhysicalDrive" << driveNumber << "' exactly to continue: ";
        std::getline(std::cin, confirmation);
        if (confirmation != "RESTORE PhysicalDrive" + std::to_string(driveNumber)) {
            std::cout << "[-] Aborted. No disk sectors were modified.\n";
            CloseHandle(hDisk);
            return 0;
        }
        bool restored = RestoreFromBackupFile(restorePath, hDisk, diskSectors);
        CloseHandle(hDisk);
        return restored ? 0 : 1;
    }

    std::vector<uint8_t> originalMBR(SECTOR_SIZE);
    if (!ReadLBA(hDisk, 0, originalMBR.data(), originalMBR.size())) {
        std::cerr << "[-] Error: Failed to read sector 0.\n";
        CloseHandle(hDisk);
        return 1;
    }

    GPTCopy primary;
    GPTCopy backup;
    std::string error;
    if (!LoadGPTCopy(hDisk, diskSectors, 1, primary, error) ||
        primary.header.backupLBA != diskSectors - 1 ||
        !LoadGPTCopy(hDisk, diskSectors, diskSectors - 1, backup, error) ||
        backup.header.backupLBA != 1) {
        std::cerr << "[-] Error: " << (error.empty() ? "Primary and backup GPT headers are inconsistent." : error) << "\n";
        CloseHandle(hDisk);
        return 1;
    }
    if (primary.header.firstUsableLBA != backup.header.firstUsableLBA ||
        primary.header.lastUsableLBA != backup.header.lastUsableLBA ||
        primary.header.numPartitionEntries != backup.header.numPartitionEntries ||
        primary.header.sizeOfPartitionEntry != backup.header.sizeOfPartitionEntry ||
        primary.header.partitionEntryArrayCRC32 != backup.header.partitionEntryArrayCRC32 ||
        primary.entriesBytes != backup.entriesBytes ||
        primary.entriesSectors != backup.entriesSectors ||
        std::memcmp(primary.header.diskGUID, backup.header.diskGUID, sizeof(primary.header.diskGUID)) != 0 ||
        !std::equal(primary.entries.begin(),
                    primary.entries.begin() + static_cast<std::ptrdiff_t>(primary.entriesBytes),
                    backup.entries.begin())) {
        std::cerr << "[-] Error: Primary and backup GPT copies do not match.\n";
        CloseHandle(hDisk);
        return 1;
    }

    if (primary.header.partitionEntryLBA < 2 ||
        primary.header.partitionEntryLBA + primary.entriesSectors > primary.header.firstUsableLBA ||
        backup.header.partitionEntryLBA <= primary.header.lastUsableLBA ||
        backup.header.partitionEntryLBA + backup.entriesSectors > backup.header.currentLBA ||
        RangesOverlap(primary.header.partitionEntryLBA, primary.entriesSectors,
                      backup.header.partitionEntryLBA, backup.entriesSectors)) {
        std::cerr << "[-] Error: GPT metadata ranges overlap or are outside expected GPT areas.\n";
        CloseHandle(hDisk);
        return 1;
    }
    if (static_cast<uint16_t>(originalMBR[510] | (originalMBR[511] << 8)) != 0xAA55) {
        std::cerr << "[-] Error: Sector 0 does not contain a valid MBR signature.\n";
        CloseHandle(hDisk);
        return 1;
    }
    bool protectiveMBRFound = false;
    for (size_t i = 0; i < 4; ++i) {
        MBRPartitionRecord existingRecord{};
        std::memcpy(&existingRecord, originalMBR.data() + 446 + i * sizeof(existingRecord),
                    sizeof(existingRecord));
        if (existingRecord.partitionType == 0xEE) {
            uint32_t expectedProtectiveSize = static_cast<uint32_t>(
                std::min<uint64_t>(diskSectors - 1, MAX_MBR_LBA));
            if (i != 0 || protectiveMBRFound || existingRecord.startingLBA != 1 ||
                existingRecord.sectorCount != expectedProtectiveSize) {
                std::cerr << "[-] Error: Protective MBR entry is malformed; refusing conversion.\n";
                CloseHandle(hDisk);
                return 1;
            }
            protectiveMBRFound = true;
        } else if (existingRecord.partitionType != 0) {
            std::cerr << "[-] Error: Existing non-protective MBR entries are present; refusing to overwrite them.\n";
            CloseHandle(hDisk);
            return 1;
        }
    }
    if (!protectiveMBRFound) {
        std::cerr << "[-] Error: A standard protective MBR entry was not found.\n";
        CloseHandle(hDisk);
        return 1;
    }

    std::vector<GPTPartitionEntry> activePartitions;
    std::vector<uint8_t> mbrTypes;
    const uint8_t emptyGUID[16] = {};
    for (uint32_t i = 0; i < primary.header.numPartitionEntries; ++i) {
        GPTPartitionEntry entry{};
        const size_t offset = static_cast<size_t>(i) * primary.header.sizeOfPartitionEntry;
        std::memcpy(&entry, primary.entries.data() + offset, sizeof(entry));
        if (std::memcmp(entry.typeGUID, emptyGUID, sizeof(emptyGUID)) == 0) {
            continue;
        }
        if (std::memcmp(entry.uniqueGUID, emptyGUID, sizeof(emptyGUID)) == 0) {
            std::cerr << "[-] Error: A used GPT entry has an empty unique GUID.\n";
            CloseHandle(hDisk);
            return 1;
        }
        for (const GPTPartitionEntry& existing : activePartitions) {
            if (std::memcmp(entry.uniqueGUID, existing.uniqueGUID, sizeof(entry.uniqueGUID)) == 0) {
                std::cerr << "[-] Error: GPT partition unique GUIDs are duplicated.\n";
                CloseHandle(hDisk);
                return 1;
            }
        }
        if (activePartitions.size() == 4) {
            std::cerr << "[-] Error: MBR supports at most four primary partitions; refusing conversion.\n";
            CloseHandle(hDisk);
            return 1;
        }
        uint8_t mbrType = 0;
        if (!MapPartitionType(entry.typeGUID, mbrType)) {
            std::cerr << "[-] Error: Partition " << (activePartitions.size() + 1)
                      << " has an unsupported GPT type; refusing to guess an MBR type.\n";
            CloseHandle(hDisk);
            return 1;
        }
        if (entry.attributes != 0 || entry.startingLBA > entry.endingLBA ||
            entry.startingLBA < primary.header.firstUsableLBA ||
            entry.endingLBA > primary.header.lastUsableLBA ||
            entry.endingLBA > MAX_MBR_LBA) {
            std::cerr << "[-] Error: Partition bounds or attributes are not safely representable in MBR.\n";
            CloseHandle(hDisk);
            return 1;
        }
        uint64_t sectorCount = entry.endingLBA - entry.startingLBA + 1;
        if (sectorCount > MAX_MBR_LBA) {
            std::cerr << "[-] Error: Partition sector count exceeds the MBR limit.\n";
            CloseHandle(hDisk);
            return 1;
        }
        activePartitions.push_back(entry);
        mbrTypes.push_back(mbrType);
    }
    if (activePartitions.empty()) {
        std::cerr << "[-] Error: GPT contains no usable partitions.\n";
        CloseHandle(hDisk);
        return 1;
    }

    std::vector<size_t> byStart(activePartitions.size());
    for (size_t i = 0; i < byStart.size(); ++i) byStart[i] = i;
    std::sort(byStart.begin(), byStart.end(), [&](size_t left, size_t right) {
        return activePartitions[left].startingLBA < activePartitions[right].startingLBA;
    });
    for (size_t i = 1; i < byStart.size(); ++i) {
        if (activePartitions[byStart[i]].startingLBA <= activePartitions[byStart[i - 1]].endingLBA) {
            std::cerr << "[-] Error: GPT partitions overlap; refusing conversion.\n";
            CloseHandle(hDisk);
            return 1;
        }
    }

    MBRBootSector newMBR{};
    std::memcpy(&newMBR, originalMBR.data(), sizeof(newMBR));
    std::memset(newMBR.partitions, 0, sizeof(newMBR.partitions));
    newMBR.bootSignature = 0xAA55;
    for (size_t i = 0; i < activePartitions.size(); ++i) {
        MBRPartitionRecord& record = newMBR.partitions[i];
        const GPTPartitionEntry& entry = activePartitions[i];
        record.bootIndicator = 0;
        record.startHead = 0xFE;
        record.startSector = 0xFF;
        record.startCylinder = 0xFF;
        record.partitionType = mbrTypes[i];
        record.endHead = 0xFE;
        record.endSector = 0xFF;
        record.endCylinder = 0xFF;
        record.startingLBA = static_cast<uint32_t>(entry.startingLBA);
        record.sectorCount = static_cast<uint32_t>(entry.endingLBA - entry.startingLBA + 1);
    }

    std::cout << "[+] Preflight passed: both GPT copies and CRCs match; "
              << activePartitions.size() << " non-overlapping supported partition(s) found.\n";
    std::cout << "[+] Partition data sectors will not be written. GPT metadata and sector 0 will change.\n";
    if (!apply) {
        std::cout << "[*] Read-only preflight complete. No disk sectors were modified.\n";
        std::cout << "    To convert, provide --apply and a new backup file on a different physical disk.\n";
        CloseHandle(hDisk);
        return 0;
    }

    std::cout << "\nWARNING: Verify that '" << backupPath
              << "' is stored on a different physical disk. The program's backup contains GPT/MBR metadata only.\n";
    std::string backupConfirmation;
    std::cout << "Type 'BACKUP ON ANOTHER DISK' exactly to confirm: ";
    std::getline(std::cin, backupConfirmation);
    if (backupConfirmation != "BACKUP ON ANOTHER DISK") {
        std::cout << "[-] Aborted. No disk sectors were modified.\n";
        CloseHandle(hDisk);
        return 0;
    }

    std::string confirmation;
    std::cout << "Type 'CONVERT PhysicalDrive" << driveNumber << "' exactly to continue: ";
    std::getline(std::cin, confirmation);
    if (confirmation != "CONVERT PhysicalDrive" + std::to_string(driveNumber)) {
        std::cout << "[-] Aborted. No disk sectors were modified.\n";
        CloseHandle(hDisk);
        return 0;
    }

    GPTBackupFileHeader backupHeader{};
    std::memcpy(backupHeader.magic, "GPTMBR1", 7);
    backupHeader.version = 1;
    backupHeader.sectorSize = SECTOR_SIZE;
    backupHeader.diskSectors = diskSectors;
    backupHeader.primaryEntriesLBA = primary.header.partitionEntryLBA;
    backupHeader.backupEntriesLBA = backup.header.partitionEntryLBA;
    backupHeader.entriesSectors = primary.entriesSectors;
    backupHeader.entriesBytes = primary.entriesBytes;
    std::vector<uint8_t> backupFile;
    AppendBytes(backupFile, &backupHeader, sizeof(backupHeader));
    AppendBytes(backupFile, originalMBR.data(), originalMBR.size());
    AppendBytes(backupFile, primary.headerSector.data(), primary.headerSector.size());
    AppendBytes(backupFile, primary.entries.data(), primary.entries.size());
    AppendBytes(backupFile, backup.entries.data(), backup.entries.size());
    AppendBytes(backupFile, backup.headerSector.data(), backup.headerSector.size());
    backupHeader.payloadCRC32 = CalculateCRC32(backupFile.data() + sizeof(backupHeader),
                                               backupFile.size() - sizeof(backupHeader));
    std::memcpy(backupFile.data(), &backupHeader, sizeof(backupHeader));
    if (!CreateAndVerifyBackup(backupPath, backupFile)) {
        CloseHandle(hDisk);
        return 1;
    }
    std::cout << "[+] Metadata backup created and read-back verified: " << backupPath << "\n";
    std::cout << "[!] Keep this backup; it contains the original partition-table metadata, not partition data.\n";

    std::vector<uint8_t> zeroArray(primary.entries.size(), 0);
    std::vector<uint8_t> zeroSector(SECTOR_SIZE, 0);
    bool writeSucceeded =
        WriteLBA(hDisk, 0, reinterpret_cast<const uint8_t*>(&newMBR), sizeof(newMBR)) &&
        WriteLBA(hDisk, backup.header.partitionEntryLBA, zeroArray.data(), zeroArray.size()) &&
        WriteLBA(hDisk, backup.header.currentLBA, zeroSector.data(), zeroSector.size()) &&
        WriteLBA(hDisk, primary.header.partitionEntryLBA, zeroArray.data(), zeroArray.size()) &&
        WriteLBA(hDisk, primary.header.currentLBA, zeroSector.data(), zeroSector.size()) &&
        FlushFileBuffers(hDisk);

    std::vector<uint8_t> verifyMBR(SECTOR_SIZE);
    std::vector<uint8_t> verifyArray(zeroArray.size());
    std::vector<uint8_t> verifyHeader(SECTOR_SIZE);
    if (writeSucceeded) {
        writeSucceeded =
            ReadLBA(hDisk, 0, verifyMBR.data(), verifyMBR.size()) &&
            std::memcmp(verifyMBR.data(), &newMBR, sizeof(newMBR)) == 0 &&
            ReadLBA(hDisk, primary.header.partitionEntryLBA, verifyArray.data(), verifyArray.size()) &&
            IsAllZero(verifyArray.data(), verifyArray.size()) &&
            ReadLBA(hDisk, backup.header.partitionEntryLBA, verifyArray.data(), verifyArray.size()) &&
            IsAllZero(verifyArray.data(), verifyArray.size()) &&
            ReadLBA(hDisk, 1, verifyHeader.data(), verifyHeader.size()) &&
            IsAllZero(verifyHeader.data(), verifyHeader.size()) &&
            ReadLBA(hDisk, diskSectors - 1, verifyHeader.data(), verifyHeader.size()) &&
            IsAllZero(verifyHeader.data(), verifyHeader.size());
    }

    if (!writeSucceeded) {
        std::cerr << "[-] Error: Conversion write or verification failed; attempting metadata rollback.\n";
        if (!RestoreMetadata(hDisk, 1, diskSectors - 1, primary, backup, originalMBR)) {
            std::cerr << "[-] CRITICAL: Automatic rollback failed. Do not write to the disk.\n"
                      << "    Preserve the disk and metadata backup at '" << backupPath
                      << "' for recovery.\n";
        } else {
            std::cerr << "[+] Original GPT metadata and MBR were restored. Verify the disk and backup.\n";
        }
        CloseHandle(hDisk);
        return 1;
    }

    std::cout << "\n[+] MBR table written and read-back verified; GPT metadata removed.\n";
    std::cout << "[!] Partition contents were not written, but conversion is not a bootability or data-loss guarantee.\n";
    std::cout << "[+] Original GPT metadata backup: " << backupPath << "\n";
    CloseHandle(hDisk);
    return 0;
}

#else

int main() {
    std::cerr << "This program requires Windows and must be run as Administrator.\n";
    return 1;
}

#endif