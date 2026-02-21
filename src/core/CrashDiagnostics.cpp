/**
 * CrashDiagnostics.cpp - Boot-time crash analysis & dump file management
 */

#include "core/CrashDiagnostics.h"
#include "core/UtilityEngine.h"
#include <esp_core_dump.h>

// ============================================================================
// RESET REASON DECODER
// ============================================================================

const char* CrashDiagnostics::getResetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "POWER_ON";
        case ESP_RST_EXT:       return "EXTERNAL_PIN";
        case ESP_RST_SW:        return "SOFTWARE (ESP.restart)";
        case ESP_RST_PANIC:     return "PANIC (crash/exception)";
        case ESP_RST_INT_WDT:   return "INTERRUPT_WDT (task starved)";
        case ESP_RST_TASK_WDT:  return "TASK_WDT (task hung)";
        case ESP_RST_WDT:       return "OTHER_WDT";
        case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT (power dip!)";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
}

// ============================================================================
// BOOT REASON PROCESSING (called once from setup())
// ============================================================================

void CrashDiagnostics::processBootReason(UtilityEngine* eng) {
    esp_reset_reason_t reason = esp_reset_reason();

    // Log to both Serial (early) and UtilityEngine (persistent)
    eng->warn(String("🔄 RESET REASON: ") + getResetReasonName(reason) +
                 " (code " + String((int)reason) + ")");

    switch (reason) {
        case ESP_RST_BROWNOUT:
            eng->error("⚡ BROWNOUT detected! Check power supply (USB cable, PSU capacity, motor current draw)");
            break;

        case ESP_RST_PANIC:
            handlePanicCrash(eng);
            break;

        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
            eng->error("⏱️ WATCHDOG timeout! A task is blocked or starving other tasks");
            break;

        default:
            break;
    }
}

// ============================================================================
// PANIC CRASH HANDLER — read coredump, log summary, save dump file
// ============================================================================

void CrashDiagnostics::handlePanicCrash(UtilityEngine* eng) {
    eng->error("💥 PANIC crash detected! Reading coredump from flash...");

    esp_core_dump_summary_t summary;
    esp_err_t err = esp_core_dump_get_summary(&summary);

    if (err != ESP_OK) {
        eng->error("💥 Could not read coredump (err " + String(err) +
                       ") — use Serial monitor for live backtrace");
        return;
    }

    // Log summary to Serial + WebSocket + ring buffer
    eng->error(String("💥 Crashed task: ") + summary.exc_task);
    eng->error(String("💥 Exception PC: 0x") + String(summary.exc_pc, HEX));

    String bt = "💥 Backtrace:";
    for (uint32_t i = 0; i < summary.exc_bt_info.depth && i < 16; i++) {
        bt += " 0x" + String(summary.exc_bt_info.bt[i], HEX);
    }
    eng->error(bt);

    if (summary.exc_bt_info.corrupted) {
        eng->warn("⚠️ Backtrace may be corrupted");
    }

    eng->error("💥 Full decode: pio run -t coredump-info");

    // Save dump file to LittleFS (accessible via /api/system/dumps/)
    saveDumpFile(eng, summary.exc_task, summary.exc_pc,
                 summary.exc_bt_info.bt, summary.exc_bt_info.depth,
                 summary.exc_bt_info.corrupted);
}

// ============================================================================
// DUMP FILE WRITER — addr2line-ready crash report saved to /dumps/
// ============================================================================

bool CrashDiagnostics::saveDumpFile(UtilityEngine* eng, const char* taskName,
                                     uint32_t excPC, const uint32_t* backtrace,
                                     uint32_t depth, bool corrupted) {
    if (!eng->isFilesystemReady()) {
        eng->warn("⚠️ Filesystem not ready — cannot save crash dump");
        return false;
    }

    eng->createDirectory("/dumps");

    // Cap depth to prevent buffer overrun
    if (depth > 16) depth = 16;

    // Build dump content
    String dump;
    dump.reserve(512);
    dump += "=== CRASH DUMP ===\n";
    dump += "Task: " + String(taskName) + "\n";
    dump += "PC:   0x" + String(excPC, HEX) + "\n";
    dump += "Backtrace depth: " + String(depth) + "\n";
    dump += "Corrupted: " + String(corrupted ? "YES" : "no") + "\n\n";

    // Raw backtrace (one per line)
    dump += "--- Backtrace ---\n";
    for (uint32_t i = 0; i < depth; i++) {
        dump += "  [" + String(i) + "] 0x" + String(backtrace[i], HEX) + "\n";
    }

    // Copy-paste ready decode command
    dump += "\n--- Decode command (run on PC) ---\n";
    dump += "xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/esp32s3_n16r8/firmware.elf";
    for (uint32_t i = 0; i < depth; i++) {
        dump += " 0x" + String(backtrace[i], HEX);
    }
    dump += "\n";

    // Generate filename: timestamp if NTP synced, else millis()
    String filename;
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year > (2020 - 1900)) {
        std::array<char, 20> ts{};
        strftime(ts.data(), ts.size(), "%Y%m%d_%H%M%S", &t);
        filename = "/dumps/crash_" + String(ts.data()) + ".txt";
    } else {
        filename = "/dumps/crash_" + String(millis()) + ".txt";
    }

    if (eng->writeFileAsString(filename, dump)) {
        eng->info("📁 Crash dump saved: " + filename);
        return true;
    }

    eng->error("❌ Failed to save crash dump");
    return false;
}
