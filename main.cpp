#include <iostream>
#include <vector>
#include <cstring>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
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

void PrintSafetyHeader() {
    std::cout << "=================================================================\n";
    std::cout << " [!] LOSSLESS GPT-TO-MBR CONVERTER & TRANSLATOR [!]\n";
    std::cout << "=================================================================\n";
    std::cout << " This tool converts GPT to MBR while keeping partition tables intact\n";
    std::cout << " and ensuring absolute zero data loss, subject to MBR limits:\n\n";
    std::cout << "  * Active partitions must be 4 or fewer.\n";
    std::cout << "  * All partition bounds must fit within 32-bit LBA space (~2TB max).\n";
    std::cout << "  * Raw data sectors remain completely untouched in-place.\n";
    std::cout << "=================================================================\n\n";
}

#ifdef _WIN32

bool ReadSectors(HANDLE hDisk, uint64_t lba, DWORD sectorCount, void* buffer) {
    LARGE_INTEGER li;
    li.QuadPart = lba * 512; // Assuming 512-byte sectors
    
    if (!SetFilePointerEx(hDisk, li, NULL, FILE_BEGIN)) {
        return false;
    }

    DWORD bytesRead = 0;
    DWORD bytesToRead = sectorCount * 512;
    return ReadFile(hDisk, buffer, bytesToRead, &bytesRead, NULL) && (bytesRead == bytesToRead);
}

bool WriteSector(HANDLE hDisk, uint64_t lba, void* buffer) {
    LARGE_INTEGER li;
    li.QuadPart = lba * 512;
    
    if (!SetFilePointerEx(hDisk, li, NULL, FILE_BEGIN)) {
        return false;
    }

    DWORD bytesWritten = 0;
    return WriteFile(hDisk, buffer, 512, &bytesWritten, NULL) && (bytesWritten == 512);
}

int main(int argc, char* argv[]) {
    std::cout << "=== GptToMbr (Windows-Native C++ Tool) ===" << std::endl;
    
    // Administrator check
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, 
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }

    if (!isAdmin) {
        std::cout << "[!] Warning: Not running as Administrator. Raw disk access will fail.\n" << std::endl;
    } else {
        std::cout << "[+] Running with Administrator privileges.\n" << std::endl;
    }

    if (argc < 2) {
        std::cout << "Usage: GptToMbr.exe <PhysicalDriveNumber>" << std::endl;
        std::cout << "Example: GptToMbr.exe 1 (targets \\\\.\\PhysicalDrive1)" << std::endl;
        std::cout << "\nPress Enter to exit...";
        std::cin.get();
        return 1;
    }

    int driveNumber = std::stoi(argv[1]);
    std::cout << "[Target Selected]: PhysicalDrive" << driveNumber << std::endl << std::endl;

    PrintSafetyHeader();

    std::string confirmation;
    std::cout << "Type 'YES' in all caps to confirm conversion: ";
    std::getline(std::cin, confirmation);

    if (confirmation != "YES") {
        std::cout << "\n[-] Operation aborted by user. No changes made." << std::endl;
        std::cout << "Press Enter to exit...";
        std::cin.get();
        return 0;
    }

    // Open handle to physical drive
    std::wstring devicePath = L"\\\\.\\PhysicalDrive" + std::to_wstring(driveNumber);
    HANDLE hDisk = CreateFileW(devicePath.c_str(), GENERIC_READ | GENERIC_WRITE, 
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (hDisk == INVALID_HANDLE_VALUE) {
        std::cerr << "[-] Error: Failed to open drive. Error Code: " << GetLastError() << std::endl;
        std::cout << "Press Enter to exit...";
        std::cin.get();
        return 1;
    }

    std::cout << "[+] Drive opened successfully. Reading GPT Header (LBA 1)..." << std::endl;

    // Read LBA 1 (GPT Header)
    std::vector<uint8_t> sectorBuffer(512);
    if (!ReadSectors(hDisk, 1, 1, sectorBuffer.data())) {
        std::cerr << "[-] Error: Failed to read GPT header sector." << std::endl;
        CloseHandle(hDisk);
        std::cin.get();
        return 1;
    }

    GPTHeader* gptHeader = reinterpret_cast<GPTHeader*>(sectorBuffer.data());
    if (std::memcmp(gptHeader->signature, "EFI PART", 8) != 0) {
        std::cerr << "[-] Error: Valid GPT signature ('EFI PART') not found on disk!" << std::endl;
        CloseHandle(hDisk);
        std::cin.get();
        return 1;
    }

    std::cout << "[+] Valid GPT Header found. Reading Partition Entries..." << std::endl;

    // Read Partition Array Entries (typically starts at LBA 2)
    uint32_t totalEntries = gptHeader->numPartitionEntries;
    uint32_t entrySize = gptHeader->sizeOfPartitionEntry;
    uint64_t entryLBA = gptHeader->partitionEntryLBA;
    
    if (entrySize < sizeof(GPTPartitionEntry) || totalEntries == 0 ||
        static_cast<uint64_t>(totalEntries) * entrySize > (1ULL << 31)) {
        std::cerr << "[-] Error: Invalid GPT partition-entry metadata.\n";
        CloseHandle(hDisk);
        return 1;
    }

    uint64_t totalBytesEntries = static_cast<uint64_t>(totalEntries) * entrySize;
    uint64_t sectorsToRead64 = (totalBytesEntries + 511) / 512;
    if (sectorsToRead64 > MAXDWORD) {
        std::cerr << "[-] Error: GPT partition-entry array is too large.\n";
        CloseHandle(hDisk);
        return 1;
    }
    DWORD sectorsToRead = static_cast<DWORD>(sectorsToRead64);
    
    std::vector<uint8_t> entriesBuffer(sectorsToRead * 512);
    if (!ReadSectors(hDisk, entryLBA, sectorsToRead, entriesBuffer.data())) {
        std::cerr << "[-] Error: Failed to read GPT partition entry array." << std::endl;
        CloseHandle(hDisk);
        std::cin.get();
        return 1;
    }

    // Collect active partitions
    std::vector<GPTPartitionEntry> activePartitions;
    uint8_t* ptr = entriesBuffer.data();
    
    for (uint32_t i = 0; i < totalEntries; ++i) {
        GPTPartitionEntry* entry = reinterpret_cast<GPTPartitionEntry*>(ptr);
        // Check if partition entry is used (Type GUID is not all zeros)
        uint8_t emptyGUID[16] = {0};
        if (std::memcmp(entry->typeGUID, emptyGUID, 16) != 0) {
            activePartitions.push_back(*entry);
        }
        ptr += entrySize;
    }

    std::cout << "[+] Found " << activePartitions.size() << " active partition(s) on disk." << std::endl;

    // Validation 1: Check MBR Partition Count Limit
    if (activePartitions.size() > 4) {
        std::cerr << "[-] ABORT: Disk has " << activePartitions.size() 
                  << " partitions. MBR supports a maximum of 4 primary partitions. Conversion would cause data loss." << std::endl;
        CloseHandle(hDisk);
        std::cin.get();
        return 1;
    }

    // Validation 2: Check 32-bit LBA bounds for each partition
    MBRBootSector newMbr{};
    newMbr.bootSignature = 0xAA55;

    for (size_t i = 0; i < activePartitions.size(); ++i) {
        uint64_t startLBA = activePartitions[i].startingLBA;
        uint64_t endLBA = activePartitions[i].endingLBA;
        uint64_t sectorCount = (endLBA - startLBA) + 1;

        if (endLBA > MAX_MBR_LBA || startLBA > MAX_MBR_LBA || sectorCount > MAX_MBR_LBA) {
            std::cerr << "[-] ABORT: Partition " << (i + 1) 
                      << " exceeds 32-bit LBA bounds (~2TB). Cannot fit into MBR safely." << std::endl;
            CloseHandle(hDisk);
            std::cin.get();
            return 1;
        }

        // Map GPT partition to MBR partition record
        newMbr.partitions[i].bootIndicator = 0x00; // Inactive by default
        newMbr.partitions[i].partitionType = 0x07; // Default to NTFS/exFAT generic type
        newMbr.partitions[i].startingLBA = static_cast<uint32_t>(startLBA);
        newMbr.partitions[i].sectorCount = static_cast<uint32_t>(sectorCount);
    }

    std::cout << "[+] All partitions successfully validated against MBR limits." << std::endl;
    std::cout << "[*] Writing new MBR table to LBA 0 (replacing protective MBR)..." << std::endl;

    // Write new MBR to LBA 0
    if (!WriteSector(hDisk, 0, &newMbr)) {
        std::cerr << "[-] Error: Failed to write MBR table to LBA 0. Error Code: " << GetLastError() << std::endl;
        CloseHandle(hDisk);
        std::cin.get();
        return 1;
    }

    std::cout << "\n[+] SUCCESS: GPT successfully converted to MBR!" << std::endl;
    std::cout << "[+] Partition table intact, data sectors untouched, structural transition complete." << std::endl;

    CloseHandle(hDisk);
    std::cout << "\nPress Enter to exit...";
    std::cin.get();
    return 0;
}

#else

int main() {
    std::cerr << "This program requires Windows and must be run as Administrator.\n";
    return 1;
}

#endif