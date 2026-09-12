#pragma once
#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

// Offline measurement transport only. Published contents are immutable.
inline void WaitForOfflineResponse(const std::filesystem::path& root, unsigned step,
    std::chrono::milliseconds timeout = std::chrono::seconds(60))
{
    const auto start = std::chrono::steady_clock::now();
    std::ofstream log(root / "marker-access.txt");
    log.exceptions(std::ios::failbit | std::ios::badbit);
    auto record = [&](const char* event, DWORD error, const std::string& detail = "") {
        log << "step=" << step << " elapsed_ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()
            << " event=" << event << " win32=" << error << " " << detail << std::endl;
    };
    DWORD lastError = ERROR_SUCCESS;
    unsigned missing = 0;
    for (;;)
    {
        if (std::filesystem::exists(root / "error.txt"))
        {
            record("worker_error", 0);
            throw std::runtime_error("Offline CPU worker rejected request");
        }
        HANDLE file = CreateFileW((root / "response.ready").c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            char bytes[64]; DWORD count = 0;
            const BOOL ok = ReadFile(file, bytes, sizeof(bytes), &count, nullptr);
            const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
            CloseHandle(file);
            if (!ok)
            {
                record("read_failed", error);
                throw std::runtime_error("Offline response read failed; Win32=" + std::to_string(error));
            }
            // Exact publisher format; never retry a successfully read wrong or malformed marker.
            const std::string observed(bytes, count);
            if (observed != std::to_string(step))
            {
                std::string hex;
                const char* digits = "0123456789abcdef";
                for (unsigned char c : observed) { hex += digits[c >> 4]; hex += digits[c & 15]; }
                record("invalid_content", 0, "bytes=" + std::to_string(count) + " hex=" + hex);
                throw std::runtime_error("Offline response content mismatch; expected=" + std::to_string(step));
            }
            record("accepted", 0, "missing_polls=" + std::to_string(missing));
            return;
        }
        lastError = GetLastError();
        if (lastError == ERROR_FILE_NOT_FOUND) ++missing;
        else record("open_failed", lastError);
        // Only missing publication and explicit sharing/locking conflicts are transient.
        if (lastError != ERROR_FILE_NOT_FOUND && lastError != ERROR_SHARING_VIOLATION && lastError != ERROR_LOCK_VIOLATION)
            throw std::runtime_error("Offline response open failed; Win32=" + std::to_string(lastError));
        if (std::chrono::steady_clock::now()-start >= timeout)
        {
            record("timeout", lastError, "missing_polls=" + std::to_string(missing));
            throw std::runtime_error("Offline response timed out; last Win32=" + std::to_string(lastError));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
