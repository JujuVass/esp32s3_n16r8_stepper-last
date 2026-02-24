# Migration WebServer → ESPAsyncWebServer

> **Branche** : `feat/async-webserver` (à créer depuis `main` @ `93fc18b`)
> **Date début** : 2026-02-23
> **Objectif** : Remplacer WebServer synchrone + links2004/WebSockets par ESPAsyncWebServer + AsyncWebSocket

---

## Pourquoi migrer

| Problème actuel | Cause | Solution Async |
|-----------------|-------|----------------|
| `_parseForm() Error: line:` pendant uploads | `Stream::readStringUntil()` bloquant dans le core Arduino | Parsing multipart async non-bloquant |
| UI freeze pendant upload 205KB (3.4s) | `handleClient()` bloque networkTask | Plus de `handleClient()` — event-driven |
| `wsMutex` (8 blocs) + flags coopératifs | `handleClient()` appelé depuis Core 0 ET Core 1 | Tout passe par la task LWIP automatiquement |
| WDT étendu à 30s (contournement) | `_parseForm()` bloque Core 0 pendant uploads longs | Peut revenir à la valeur standard |
| `serviceWebSocket()` dans CalibrationManager | Garder le web vivant pendant calibration bloquante | Plus nécessaire — web tourne tout seul |

---

## Inventaire (15 fichiers, ~146 appels)

### Headers (9 fichiers)

| Fichier | Changements |
|---------|------------|
| `include/core/GlobalState.h` | `<WebServer.h>` → `<ESPAsyncWebServer.h>`, supprimer `<WebSocketsServer.h>`, `extern AsyncWebServer server` + `extern AsyncWebSocket ws`, supprimer `extern SemaphoreHandle_t wsMutex` |
| `include/communication/APIRoutes.h` | Mêmes changements includes + externs |
| `include/communication/FilesystemManager.h` | Member `AsyncWebServer& server`, constructor `FilesystemManager(AsyncWebServer&)` |
| `include/movement/CalibrationManager.h` | Supprimer `WebServer* server_` + `WebSocketsServer* webSocket_`, simplifier `init()` |
| `include/communication/StatusBroadcaster.h` | `AsyncWebSocket* _ws`, `void begin(AsyncWebSocket*)` |
| `include/communication/CommandDispatcher.h` | `AsyncWebSocket* _ws`, `void begin(AsyncWebSocket*)` |
| `include/movement/SequenceExecutor.h` | `AsyncWebSocket* _ws`, `void begin(AsyncWebSocket*)` |
| `include/core/UtilityEngine.h` | `AsyncWebSocket& _ws` |
| `include/core/logger/Logger.h` | `AsyncWebSocket& _ws` |

### Sources (6 fichiers)

| Fichier | Appels à migrer | Points critiques |
|---------|:---:|---|
| `src/communication/APIRoutes.cpp` | ~106 | 39 routes + helpers + body access |
| `src/communication/FilesystemManager.cpp` | ~33 | Upload multipart |
| `src/StepperController.cpp` | ~10 | Instanciation, networkTask, wsMutex |
| `src/movement/CalibrationManager.cpp` | ~11 | Supprimer serviceWebSocket() entier |
| `src/movement/SequenceExecutor.cpp` | ~6 | Supprimer handleClient + wsMutex |
| `src/communication/StatusBroadcaster.cpp` | ~4 | broadcastTXT → textAll |

### Fichiers additionnels (broadcast WebSocket)

| Fichier | Changements |
|---------|------------|
| `src/communication/CommandDispatcher.cpp` | Event types : WStype_* → WS_EVT_* |
| `src/movement/SequenceTableManager.cpp` | `webSocket.broadcastTXT()` → `ws.textAll()` |
| `src/core/UtilityEngine.cpp` | `_ws.connectedClients()` → `_ws.count()` |
| `src/core/logger/Logger.cpp` | `_ws.broadcastTXT()` → `_ws.textAll()`, supprimer wsMutex |
| `platformio.ini` | lib_deps + build_flags |

---

## Mapping API

| WebServer / WebSocketsServer | ESPAsyncWebServer / AsyncWebSocket |
|---|---|
| `server.handleClient()` | **supprimé** |
| `webSocket.loop()` | **supprimé** |
| `server.on(path, method, handler)` | `server.on(path, method, [](AsyncWebServerRequest *req) {...})` |
| `server.send(code, type, body)` | `request->send(code, type, body)` |
| `server.sendHeader(key, val)` | `response->addHeader(key, val)` puis `request->send(response)` |
| `server.streamFile(file, mime)` | `request->send(LittleFS, path, mime)` |
| `server.arg("plain")` | Body handler callback (accumulate data) |
| `server.hasArg("path")` | `request->hasParam("path")` |
| `server.arg("path")` | `request->getParam("path")->value()` |
| `server.upload()` / `HTTPUpload&` | Upload handler callback params |
| `server.hasHeader(h)` | `request->hasHeader(h)` |
| `server.header(h)` | `request->getHeader(h)->value()` |
| `server.client().remoteIP()` | `request->client()->remoteIP()` |
| `server.uri()` | `request->url()` |
| `server.method()` | `request->method()` |
| `server.onNotFound(fn)` | `server.onNotFound([](AsyncWebServerRequest *req) {...})` |
| `webSocket.begin()` | `server.addHandler(&ws)` |
| `webSocket.onEvent(fn)` | `ws.onEvent(fn)` (signature différente) |
| `webSocket.broadcastTXT(msg)` | `ws.textAll(msg)` |
| `webSocket.connectedClients()` | `ws.count()` |
| `webSocket.sendTXT(id, msg)` | `ws.text(id, msg)` |
| `webSocket.remoteIP(id)` | `client->remoteIP()` (dans event callback) |

### Pattern body POST (server.arg("plain"))

```cpp
// AVANT
server.on("/api/playlists/add", HTTP_POST, handleAddPreset);
void handleAddPreset() {
    if (!server.hasArg("plain")) { sendJsonError(400, "Missing body"); return; }
    String body = server.arg("plain");
    // ... parse JSON ...
}

// APRÈS
server.on("/api/playlists/add", HTTP_POST,
    [](AsyncWebServerRequest *request) {
        // completion — called after body received
    },
    NULL, // no upload handler
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        // body handler
        String body = String((char*)data, len);
        // ... parse JSON, send response via request->send() ...
    }
);
```

### Pattern upload multipart

```cpp
// AVANT
server.on("/api/fs/upload", HTTP_POST,
    [this]() { /* completion: send response */ },
    [this]() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) { ... }
        else if (upload.status == UPLOAD_FILE_WRITE) { ... }
        else if (upload.status == UPLOAD_FILE_END) { ... }
    }
);

// APRÈS
server.on("/api/fs/upload", HTTP_POST,
    [this](AsyncWebServerRequest *request) {
        // completion: send response
        request->send(200, "application/json", R"({"success":true})");
    },
    [this](AsyncWebServerRequest *request, const String& filename, size_t index,
           uint8_t *data, size_t len, bool final) {
        if (index == 0) { /* START: open file */ }
        if (len > 0)    { /* WRITE: write data */ }
        if (final)       { /* END: flush + close */ }
    }
);
```

### Pattern WebSocket events

```cpp
// AVANT (links2004/WebSockets)
webSocket.onEvent([](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:    /* ... */ break;
        case WStype_DISCONNECTED: /* ... */ break;
        case WStype_TEXT:         /* ... */ break;
    }
});

// APRÈS (AsyncWebSocket)
ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,
              AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:    /* client->remoteIP() */ break;
        case WS_EVT_DISCONNECT: /* ... */ break;
        case WS_EVT_DATA: {
            AwsFrameInfo *info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                String msg = String((char*)data, len);
                /* ... handle command ... */
            }
            break;
        }
    }
});
```

---

## Phases d'exécution

### Phase 1 — Dépendances
- [ ] Créer branche `feat/async-webserver`
- [ ] `platformio.ini` : remplacer `links2004/WebSockets` par `mathieucarbou/ESPAsyncWebServer@^3.6.0` + `mathieucarbou/AsyncTCP@^3.3.2`
- [ ] Supprimer `-Wno-deprecated-declarations`

### Phase 2 — Headers & Types
- [ ] `include/core/GlobalState.h`
- [ ] `include/communication/APIRoutes.h`
- [ ] `include/communication/FilesystemManager.h`
- [ ] `include/movement/CalibrationManager.h`
- [ ] `include/communication/StatusBroadcaster.h`
- [ ] `include/communication/CommandDispatcher.h`
- [ ] `include/movement/SequenceExecutor.h`
- [ ] `include/core/UtilityEngine.h`
- [ ] `include/core/logger/Logger.h`

### Phase 3 — StepperController.cpp (coeur)
- [ ] Instanciation `AsyncWebServer` + `AsyncWebSocket`
- [ ] Supprimer `wsMutex` (création + tous usages)
- [ ] Simplifier `networkTask()` : supprimer `handleClient()` + `webSocket.loop()`
- [ ] Adapter `init()` appels modules
- [ ] Adapter / supprimer WDT 30s extension

### Phase 4 — APIRoutes.cpp helpers
- [ ] `sendJsonError()` → prend `AsyncWebServerRequest*`
- [ ] `sendJsonSuccess()` → prend `AsyncWebServerRequest*`
- [ ] `sendJsonData()` → prend `AsyncWebServerRequest*`
- [ ] `serveStaticFile()` → `request->send(LittleFS, ...)`
- [ ] `handleCORSPreflight()` → `AsyncWebServerResponse*`
- [ ] `handleNotFound()` → `request->url()` + `request->method()`
- [ ] **Compilation intermédiaire**

### Phase 5 — APIRoutes.cpp routes (39 + onNotFound)
- [ ] Routes GET simples (stats, playlists, ip, ping, logs, wifi, system)
- [ ] Routes POST avec body (playlists/add, stats/import, command, wifi/save, sequence/import...)
- [ ] Routes captive portal (generate_204, hotspot-detect, fwlink...)
- [ ] `server.onNotFound()`
- [ ] **Compilation intermédiaire**

### Phase 6 — FilesystemManager.cpp
- [ ] Routes GET (list, download, read, /filesystem)
- [ ] Route POST write
- [ ] Route POST upload (multipart async)
- [ ] Route POST delete, clear
- [ ] Supprimer `UPLOAD_POST_CLOSE_DELAY_MS` (si inutile avec async)
- [ ] **Compilation intermédiaire + test `--sync-force`**

### Phase 7 — Modules WebSocket
- [ ] `src/communication/StatusBroadcaster.cpp` : `broadcastTXT` → `textAll`, `connectedClients` → `count`
- [ ] `src/communication/CommandDispatcher.cpp` : adapter `onWebSocketEvent()` signature + event types
- [ ] `src/movement/SequenceExecutor.cpp` : `broadcastTXT` → `textAll`, supprimer `handleClient` + `wsMutex`
- [ ] `src/movement/SequenceTableManager.cpp` : `broadcastTXT` → `textAll`
- [ ] `src/core/UtilityEngine.cpp` : `connectedClients` → `count`
- [ ] `src/core/logger/Logger.cpp` : `broadcastTXT` → `textAll`, supprimer wsMutex guard
- [ ] **Compilation intermédiaire**

### Phase 8 — CalibrationManager.cpp
- [ ] Supprimer `serviceWebSocket()` et `serviceWebSocketIfDue()`
- [ ] Supprimer `server_->handleClient()` (3 sites)
- [ ] Supprimer `webSocket_->loop()` (3 sites)
- [ ] Supprimer `wsMutex` usage (5 blocs)
- [ ] Adapter `init()` : plus besoin de WebServer*/WebSocketsServer*
- [ ] **Compilation intermédiaire**

### Phase 9 — Nettoyage final
- [ ] Supprimer `wsMutex` declaration dans GlobalState.h
- [ ] Supprimer `wsMutex` creation dans StepperController.cpp
- [ ] Vérifier `calibrationInProgress` / `blockingMoveInProgress` — simplifier si possible
- [ ] Supprimer `uploadStopDone` flag si simplifié
- [ ] Réduire WDT timeout (revenir au standard ou 10s)
- [ ] Supprimer `UPLOAD_POST_CLOSE_DELAY_MS` de Config.h si confirmé inutile
- [ ] **Build complet + test intégration**
- [ ] **Test upload `--sync-force` (28 fichiers)**
- [ ] **Test WebSocket live (status, commands)**
- [ ] **Test calibration**
- [ ] **Test séquences avec mouvements bloquants**
- [ ] Commit final + merge dans main

---

## Suppressions attendues

| Élément | Fichiers | Raison |
|---------|----------|--------|
| `wsMutex` (décl + création + 8 blocs take/give) | GlobalState.h, StepperController.cpp, CalibrationManager.cpp, SequenceExecutor.cpp, Logger.cpp | Plus de polling synchrone |
| `server.handleClient()` (6 appels) | StepperController.cpp, CalibrationManager.cpp, SequenceExecutor.cpp | Event-driven |
| `webSocket.loop()` (6 appels) | StepperController.cpp, CalibrationManager.cpp, SequenceExecutor.cpp | Event-driven |
| `serviceWebSocket()` / `serviceWebSocketIfDue()` | CalibrationManager.cpp/.h | Plus nécessaire |
| `UPLOAD_POST_CLOSE_DELAY_MS` | Config.h, FilesystemManager.cpp | Plus de `_parseForm()` bloquant |
| WDT 30s extension | StepperController.cpp | Plus de blocage `_parseForm()` |
| `-Wno-deprecated-declarations` | platformio.ini | Était pour WebSockets 2.7.3 |
| `links2004/WebSockets` | platformio.ini | Remplacé par AsyncWebSocket intégré |

---

## Risques & Mitigation

| Risque | Impact | Mitigation |
|--------|--------|------------|
| Race conditions (handlers async accèdent aux globals) | Moyen | Audit thread-safety, protéger les structures partagées critiques |
| AsyncWebSocket `textAll()` depuis Core 1 (motorTask) | Moyen | Vérifier thread-safety de `textAll()` ou garder un mutex minimal pour broadcast |
| RAM supplémentaire (~10KB) | Faible | ESP32-S3 N16R8 = 8MB PSRAM, négligeable |
| Fork ESPAsyncWebServer instable | Faible | mathieucarbou fork est le plus maintenu (2024-2026) |
| Captive portal DNS + redirect | Faible | Routes captive portal transposables 1:1 |

---

## Rollback

```bash
git checkout main
# ou
git revert --no-commit feat/async-webserver
```

Le commit `93fc18b` sur `main` est le dernier état stable avec WebServer synchrone.
