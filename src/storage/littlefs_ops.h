// storage/littlefs_ops.h
// Wrappers around LittleFS. All paths live here.

#pragma once

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

namespace Storage {

// Basename length including NUL. "AA-BB-CC-DD-EE-FF-9.pcap" is 24+NUL.
static const uint8_t FILE_NAME_MAX = 32;

bool begin();

struct Stats {
    size_t total;
    size_t used;
    size_t free;
    uint16_t handshakes;
    uint16_t results;
};
Stats stats();

const char* baseName(const char* path);

// Visit each regular file in /handshakes/. name is basename only.
typedef void (*FileVisitor)(const char* name, size_t size, void* ctx);
uint16_t forEachHandshake(FileVisitor fn, void* ctx);

uint16_t listHandshakes(char out[][FILE_NAME_MAX], uint16_t max);
uint16_t listResults(char out[][FILE_NAME_MAX], uint16_t max);

const char* const DIR_HANDSHAKES = "/handshakes";
const char* const DIR_RESULTS    = "/results";
const char* const FILE_WPASEC_RESULTS    = "/results/wpasec.txt";
const char* const FILE_WPASEC_UPLOADED   = "/results/wpasec_uploaded.txt";
const char* const FILE_PWNCRACK_RESULTS  = "/results/pwncrack.txt";
const char* const FILE_PWNCRACK_UPLOADED = "/results/pwncrack_uploaded.txt";

bool ensureDir(const char* path);
bool removeFile(const char* path);
bool fileExists(const char* path);
size_t fileSize(const char* path);
bool formatStorage();

} // namespace Storage
