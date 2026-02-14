/**
 * ============================================================================
 * SEQUENCE TABLE MANAGER MODULE
 * ============================================================================
 * Handles sequence table CRUD operations, import/export, validation.
 * 
 * Features:
 * - Add/Delete/Update sequence lines
 * - Move/Toggle/Duplicate lines
 * - Physics validation (distance limits)
 * - JSON import/export
 * - WebSocket broadcasting
 * 
 * Note: Execution logic remains in main (processSequenceExecution)
 * 
 * @author Modular Refactoring
 * @version 1.0
 */

#ifndef SEQUENCE_TABLE_MANAGER_H
#define SEQUENCE_TABLE_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "core/Types.h"
#include "core/Config.h"
#include "core/UtilityEngine.h"

// ============================================================================
// CONSTANTS
// ============================================================================

#ifndef MAX_SEQUENCE_LINES
#define MAX_SEQUENCE_LINES 20
#endif

#include "core/GlobalState.h"

// ============================================================================
// SEQUENCE TABLE MANAGER CLASS
// ============================================================================

class SequenceTableManager {
public:
  // Singleton access
  static SequenceTableManager& getInstance() {
    static SequenceTableManager instance;
    return instance;
  }
  
  // ========================================================================
  // CRUD OPERATIONS
  // ========================================================================
  
  /**
   * Add a new line to the sequence table
   * @param newLine Line to add
   * @return lineId if successful, -1 if table is full
   */
  int addLine(const SequenceLine& newLine);
  
  /**
   * Delete a line by ID
   * @param lineId Line ID to delete
   * @return true if deleted, false if not found
   */
  bool deleteLine(int lineId);
  
  /**
   * Update an existing line
   * @param lineId Line ID to update
   * @param updatedLine New line data
   * @return true if updated, false if not found
   */
  bool updateLine(int lineId, const SequenceLine& updatedLine);
  
  /**
   * Move a line up (-1) or down (+1)
   * @param lineId Line ID to move
   * @param direction -1 for up, +1 for down
   * @return true if moved, false if invalid
   */
  bool moveLine(int lineId, int direction);
  
  /**
   * Reorder a line to a specific index (for drag & drop)
   * @param lineId Line ID to move
   * @param newIndex Target index (0-based)
   * @return true if reordered, false if invalid
   */
  bool reorderLine(int lineId, int newIndex);
  
  /**
   * Toggle line enabled/disabled
   * @param lineId Line ID
   * @param enabled New state
   * @return true if toggled, false if not found
   */
  bool toggleLine(int lineId, bool enabled);
  
  /**
   * Duplicate a line
   * @param lineId Line ID to duplicate
   * @return new lineId if successful, -1 if failed
   */
  int duplicateLine(int lineId);
  
  /**
   * Clear entire sequence table
   */
  void clear();
  
  // ========================================================================
  // VALIDATION
  // ========================================================================
  
  /**
   * Validate sequence line against physical constraints
   * @param line Line to validate
   * @return error message if invalid, empty string if valid
   */
  String validatePhysics(const SequenceLine& line);
  
  // ========================================================================
  // JSON OPERATIONS
  // ========================================================================
  
  /**
   * Parse SequenceLine from JSON object
   * @param obj JSON object
   * @return Parsed SequenceLine
   */
  SequenceLine parseFromJson(JsonVariantConst obj);
  
  /**
   * Export sequence table to JSON string
   * @return JSON string
   */
  String exportToJson();
  
  /**
   * Import sequence from JSON string
   * @param jsonData JSON string
   * @return number of lines imported, -1 if error
   */
  int importFromJson(String jsonData);
  
  // ========================================================================
  // BROADCASTING
  // ========================================================================
  
  /**
   * Broadcast sequence table to all WebSocket clients
   */
  void broadcast();
  
  /**
   * Send JSON response wrapper
   */
  void sendJsonResponse(const char* type, const String& data);
  
  // ========================================================================
  // DATA ACCESS (uses extern globals from main)
  // ========================================================================
  
  /**
   * Find line index by ID
   * @param lineId Line ID
   * @return index if found, -1 if not found
   */
  int findLineIndex(int lineId);
  
private:
  // Singleton - prevent construction/copying
  SequenceTableManager();
  SequenceTableManager(const SequenceTableManager&) = delete;
  SequenceTableManager& operator=(const SequenceTableManager&) = delete;
  
  // Uses global 'engine' pointer for logging (extern UtilityEngine* engine)
};

// Global singleton instance
extern SequenceTableManager& SeqTable;

// ============================================================================
// SEQUENCE DATA - Owned by SequenceTableManager
// ============================================================================
// Defined in SequenceTableManager.cpp, accessible via extern:
extern SequenceLine sequenceTable[MAX_SEQUENCE_LINES];
extern int sequenceLineCount;

#endif // SEQUENCE_TABLE_MANAGER_H
