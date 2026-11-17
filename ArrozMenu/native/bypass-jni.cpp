#include <jni.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {
using Address = std::uintptr_t;

struct Region {
    Address start;
    Address end;
    int protection;
    bool protectionChanged = false;
};

struct Patch {
    Address end;
    std::vector<Region> regions;
    std::vector<jbyte> original;
    bool bytesChanged = false;
};

std::mutex patchMutex;
std::map<Address, Patch> patches;

bool readRegions(Address start, Address end, std::vector<Region>& regions) {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (start < end && std::getline(maps, line)) {
        unsigned long long from = 0;
        unsigned long long to = 0;
        char flags[5]{};
        if (std::sscanf(line.c_str(), "%llx-%llx %4s", &from, &to, flags) != 3 || to <= start) {
            continue;
        }
        if (from > start || flags[0] != 'r' || flags[3] != 'p') {
            return false;
        }
        const auto regionEnd = to < end ? static_cast<Address>(to) : end;
        int protection = PROT_READ;
        if (flags[1] == 'w') protection |= PROT_WRITE;
        if (flags[2] == 'x') protection |= PROT_EXEC;
        regions.push_back({start, regionEnd, protection});
        start = regionEnd;
    }
    return start == end;
}

bool changeProtection(const Region& region, int protection) {
    return mprotect(reinterpret_cast<void*>(region.start), region.end - region.start, protection) == 0;
}

bool allowWriting(Patch& patch) {
    for (auto& region : patch.regions) {
        const int writable = (region.protection | PROT_WRITE) & ~PROT_EXEC;
        if (!region.protectionChanged && writable == region.protection) continue;
        region.protectionChanged = true;
        if (!changeProtection(region, writable)) return false;
    }
    return true;
}

bool restoreProtections(Patch& patch) {
    bool success = true;
    for (auto& region : patch.regions) {
        if (!region.protectionChanged) continue;
        if (changeProtection(region, region.protection)) {
            region.protectionChanged = false;
        } else {
            success = false;
        }
    }
    return success;
}

bool writeAndCheck(Address start, const std::vector<jbyte>& bytes) {
    auto* destination = reinterpret_cast<char*>(start);
    std::memcpy(destination, bytes.data(), bytes.size());
    __builtin___clear_cache(destination, destination + bytes.size());
    return std::memcmp(destination, bytes.data(), bytes.size()) == 0;
}

bool rollback(Address start, Patch& patch) {
    if (patch.bytesChanged && allowWriting(patch) && writeAndCheck(start, patch.original)) {
        patch.bytesChanged = false;
    }
    const bool protectionsRestored = restoreProtections(patch);
    return !patch.bytesChanged && protectionsRestored &&
           std::memcmp(reinterpret_cast<const void*>(start), patch.original.data(),
                       patch.original.size()) == 0;
}

bool overlaps(Address start, Address end, Address pageStart, Address pageEnd) {
    for (const auto& [otherStart, patch] : patches) {
        if (start < patch.end && otherStart < end) return true;
        for (const auto& region : patch.regions) {
            if (region.protectionChanged && pageStart < region.end && region.start < pageEnd) {
                return true;
            }
        }
    }
    return false;
}

// The caller must keep the target mapped and stop other threads from using it.
bool apply(Address start, const std::vector<jbyte>& replacement,
           const std::vector<jbyte>& expected) {
    const long pageSize = sysconf(_SC_PAGESIZE);
    const auto limit = std::numeric_limits<Address>::max();
    if (pageSize <= 0 || start == 0 || replacement.empty() ||
        replacement.size() != expected.size() || replacement.size() > limit - start) {
        return false;
    }

    const Address page = static_cast<Address>(pageSize);
    const Address end = start + replacement.size();
    const Address lastPage = (end - 1) - (end - 1) % page;
    if (lastPage > limit - page) return false;
    const Address pageStart = start - start % page;
    const Address pageEnd = lastPage + page;

    std::lock_guard<std::mutex> lock(patchMutex);
    if (overlaps(start, end, pageStart, pageEnd)) return false;

    Patch pending{end, {}, std::vector<jbyte>(expected.size()), false};
    if (!readRegions(pageStart, pageEnd, pending.regions)) return false;

    std::memcpy(pending.original.data(), reinterpret_cast<const void*>(start), expected.size());
    if (pending.original != expected) return false;

    const auto [it, inserted] = patches.emplace(start, std::move(pending));
    if (!inserted) return false;

    Patch& patch = it->second;
    if (allowWriting(patch)) {
        patch.bytesChanged = true;
        const bool bytesWritten = writeAndCheck(start, replacement);
        const bool protectionsRestored = restoreProtections(patch);
        if (bytesWritten && protectionsRestored) return true;
    }
    if (rollback(start, patch)) patches.erase(it);
    return false;
}
}

extern "C" JNIEXPORT void JNICALL
Java_com_rdzim_lucasmenu_keyauth_Bypass_init(JNIEnv*, jclass) {}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_rdzim_lucasmenu_keyauth_Bypass_applyPatch(
    JNIEnv* env, jclass, jlong targetAddr, jbyteArray newBytes, jbyteArray expectedOriginal) {
    if (env->ExceptionCheck() || !newBytes || !expectedOriginal) return JNI_FALSE;

    const jsize size = env->GetArrayLength(newBytes);
    if (env->ExceptionCheck() || size <= 0) return JNI_FALSE;

    const jsize expectedSize = env->GetArrayLength(expectedOriginal);
    if (env->ExceptionCheck() || size != expectedSize) return JNI_FALSE;

    try {
        std::vector<jbyte> replacement(size);
        std::vector<jbyte> expected(size);
        env->GetByteArrayRegion(newBytes, 0, size, replacement.data());
        if (env->ExceptionCheck()) return JNI_FALSE;
        env->GetByteArrayRegion(expectedOriginal, 0, size, expected.data());
        if (env->ExceptionCheck()) return JNI_FALSE;
        return apply(static_cast<Address>(targetAddr), replacement, expected) ? JNI_TRUE : JNI_FALSE;
    } catch (...) {
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_rdzim_lucasmenu_keyauth_Bypass_restorePatch(JNIEnv*, jclass, jlong targetAddr) {
    try {
        std::lock_guard<std::mutex> lock(patchMutex);
        const Address start = static_cast<Address>(targetAddr);
        const auto it = patches.find(start);
        if (it == patches.end() || !rollback(start, it->second)) return JNI_FALSE;
        patches.erase(it);
        return JNI_TRUE;
    } catch (...) {
        return JNI_FALSE;
    }
}
