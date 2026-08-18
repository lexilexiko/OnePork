# OINK: чеклист правок после рефакторинга

Контекст: после сравнения с оригиналом 0ct0sec/M5PORKCHOP (v0.1.8b-PSTH)
найдены три отклонения, каждое из которых безопасно по отдельности, но вместе
дают эффект "ловит, потом перестаёт". Чеклист — порядок применения и
минимально-необходимые правки.

---

## Корневая причина

1. Убраны reserve() после clear() в init/stop -> векторы растут с
   capacity 0, каждый push_back делает realloc по степеням 2 ->
   фрагментация heap -> heap gates в findOrCreateHandshakeSafe блокируют
   создание записей через несколько часов работы -> CAS дропает EAPOL.
2. CAS acquire в promiscuous callback роняет фрейм, если main thread ещё
   читает слот. Оригинал просто ждёт через break. У тебя в неудачный
   момент M2 вслед за M1 дропается -> handshake не complete -> через раз
   не ловится.
3. Долгое окно setBusy(true) в update() — beacon для target AP
   пропускается (NetworkRecon::isBusy() гейтит его в твоём callback).

---

## Правка №1 — вернуть reserve() в init()

Файл: src/modes/oink.cpp
Функция: OinkMode::init()
Строки: ~428-432

БЫЛО:
```
    // networks vector is now managed by NetworkRecon - just clear OINK-specific data
    // No shrink_to_fit / reserve — same abort as HASHES on a fragmented heap.
    handshakes.clear();
    pmkids.clear();
```

СТАЛО:
```
    // networks vector is now managed by NetworkRecon - just clear OINK-specific data
    // shrink_to_fit avoided (abort on fragmented heap), but reserve() keeps capacity
    // stable so push_back doesn't trigger exponential reallocs during long sessions.
    handshakes.clear();
    pmkids.clear();
    handshakes.reserve(8);    // matches pre-fault heuristic from original
    pmkids.reserve(16);
```

Зачем: оригинал использует reserve(5)/reserve(10). 0 — хуже всего, каждый
хендшейк это realloc. 8 и 16 — компромисс: меньше 1KB и погасит
фрагментацию.

---

## Правка №2 — вернуть reserve() в stop()

Файл: src/modes/oink.cpp
Функция: OinkMode::stop()
Строки: ~600-602

БЫЛО:
```
    // Clear lists. Do not shrink_to_fit — abort() on fragmented heap (no exceptions).
    handshakes.clear();
    pmkids.clear();
```

СТАЛО:
```
    // Clear lists. Do not shrink_to_fit — abort() on fragmented heap (no exceptions).
    // But DO reserve to keep capacity bounded on next start() and avoid realloc churn.
    handshakes.clear();
    pmkids.clear();
    handshakes.reserve(8);
    pmkids.reserve(16);
```

Зачем: после stop() → start() ёмкость может быть 0 (если был первый
запуск со shrink_to_fit'ом в прошлом) — снова realloc на первом хендшейке.

---

## Правка №3 — убрать CAS-dropa из callback (самое важное для ловли)

Файл: src/modes/oink.cpp
Функция: OinkMode::processEAPOL()
Строки: ~2596-2616

БЫЛО:
```
    // Store frame in target slot if we have one.
    // CAS acquire: prevents concurrent access with consumer (main loop, core 0).
    // If CAS fails (consumer is reading this slot), drop the frame —
    // EAPOL handshakes are retransmitted so we'll catch the next one.
    if (!frameWrittenInAlloc && targetSlot < PENDING_HS_SLOTS && pendingHandshakes[targetSlot]) {
        bool expected = false;
        if (pendingHsBusy[targetSlot].compare_exchange_strong(expected, true)) {
            if (frameIdx < 4) {
                // Save if slot empty or new frame has better RSSI.
                PendingHandshakeFrame* slot = pendingHandshakes[targetSlot];
                if (slot->frames[frameIdx].len == 0 || slot->frames[frameIdx].rssi < rssi) {
                    // EAPOL payload for hashcat 22000
                    uint16_t copyLen = min((uint16_t)512, len);
                    memcpy(slot->frames[frameIdx].data, payload, copyLen);
                    slot->frames[frameIdx].len = copyLen;

                    // Full 802.11 frame for PCAP export
                    uint16_t fullCopyLen = min((uint16_t)300, fullFrameLen);
                    memcpy(slot->frames[frameIdx].fullFrame, fullFrame, fullCopyLen);
                    slot->frames[frameIdx].fullFrameLen = fullCopyLen;
                    slot->frames[frameIdx].rssi = rssi;

                    slot->capturedMask |= (1 << frameIdx);
                }
            }
            pendingHsBusy[targetSlot].store(false);  // Release CAS lock
        }
    }
```

СТАЛО:
```
    // Store frame in target slot.
    // Match original: if slot is currently busy (main thread reading), don't drop
    // the EAPOL frame — spin briefly waiting for busy=false. M2 arriving during
    // main-thread handoff is FATAL if dropped (handshake becomes incomplete).
    // Next retransmit ~100ms later is too late when M1+M2 must be tight pair.
    if (!frameWrittenInAlloc && targetSlot < PENDING_HS_SLOTS && pendingHandshakes[targetSlot]) {
        // Spin briefly waiting for busy=false.
        // EAPOL burst is 4 frames within ~10ms — main thread processing
        // takes <1ms, so 64 spins (~tens of µs) is plenty.
        int spins = 0;
        bool expected = false;
        while (spins++ < 64 && !pendingHsBusy[targetSlot].compare_exchange_strong(expected, true)) {
            expected = false;
        }
        if (spins < 65) {
            if (frameIdx < 4) {
                PendingHandshakeFrame* slot = pendingHandshakes[targetSlot];
                if (slot->frames[frameIdx].len == 0 || slot->frames[frameIdx].rssi < rssi) {
                    uint16_t copyLen = min((uint16_t)512, len);
                    memcpy(slot->frames[frameIdx].data, payload, copyLen);
                    slot->frames[frameIdx].len = copyLen;

                    uint16_t fullCopyLen = min((uint16_t)300, fullFrameLen);
                    memcpy(slot->frames[frameIdx].fullFrame, fullFrame, fullCopyLen);
                    slot->frames[frameIdx].fullFrameLen = fullCopyLen;
                    slot->frames[frameIdx].rssi = rssi;

                    slot->capturedMask |= (1 << frameIdx);
                }
            }
            pendingHsBusy[targetSlot].store(false, std::memory_order_release);
        }
        // else: spin timeout, frame skipped. Acceptable loss vs. silent CAS drop
        // because timeout only happens if main loop stalls >~tens of µs, which is
        // already an SD/heap-pressure emergency.
    }
```

Зачем: оригинал не имеет CAS — main thread просто break-ает. Callback
всегда доводит запись до конца. У тебя CAS acquire с тихим drop: если M2
приходит когда main thread в findOrCreateHandshakeSafe, M2 молча теряется,
handshake без M2 → isComplete()=false → pendingHandshakeComplete не
выставляется → "перестало ловить". Spin-loop вместо drop: 64 итерации
~микросекунды.

---

## Правка №4 — сузить окно setBusy(true)

Файл: src/modes/oink.cpp
Функция: OinkMode::update()

Проверить (правки скорее всего не нужны, но перепроверь):
что NetworkRecon::setBusy(false) вызывается сразу после pending-PMKID
очереди, до updateTargetCache() / state machine / beacon audit.

Искать блок:
```
// RELEASE LOCK EARLY - state machine doesn't need exclusive vector access
// This minimizes packet drop window from ~10ms to ~0.5ms
NetworkRecon::setBusy(false);
```

Если он стоит ПОСЛЕ pending-PMKID-цикла и ПЕРЕД beacon audit — всё ок.
Если нет — передвинь.

Зачем: NetworkRecon::promiscuousCallback вызывает только modeCallback
когда busy=true; твой callback в case WIFI_PKT_MGMT отсекает beacon через
NetworkRecon::isBusy(). Чем короче окно busy — тем больше beacon'ов
обработается.

---

## Правка №5 — timeout в consumer-loop

Файл: src/modes/oink.cpp
Функция: OinkMode::update() (consumer для pending Hs слотов)
Строки: ~785-787

БЫЛО:
```
    // Get slot from circular buffer
    uint8_t slot = pendingHsRead;
    if (!pendingHandshakes[slot]) {
        break;  // Not allocated yet, wait for next cycle
    }
    // CAS acquire: atomically set busy=true if currently false.
    // Prevents concurrent access if the WiFi callback (core 1)
    // is updating this slot's frame data at the same time.
    bool expected = false;
    if (!pendingHsBusy[slot].compare_exchange_strong(expected, true)) {
        break;  // Callback is writing to this slot, wait for next cycle
    }
```

СТАЛО:
```
    // Get slot from circular buffer
    uint8_t slot = pendingHsRead;
    if (!pendingHandshakes[slot]) {
        break;  // Not allocated yet, wait for next cycle
    }
    // Spin briefly to acquire write lock (callback might be writing this slot).
    int acquireSpins = 0;
    bool expected = false;
    while (acquireSpins++ < 64 &&
           !pendingHsBusy[slot].compare_exchange_strong(expected, true)) {
        expected = false;
    }
    if (acquireSpins >= 64) {
        // Callback may have died holding the lock. Skip slot, retry next update().
        // Don't advance read pointer.
        break;
    }
```

Зачем: защита от зависшего busy=true (теоретически невозможно, но если
WDT reset'нул WiFi task в неудачный момент — main не должен spin'ить).

---

## Правка №6 — диагностические счётчики

Файл: src/modes/oink.cpp
Функция: OinkMode::promiscuousCallback() (в самом начале)

Добавить счётчики:
```
    static uint32_t eapolTotal = 0;
    static uint32_t eapolProcessed = 0;
    static uint32_t eapolDroppedBusy = 0;
    static uint32_t beaconTotal = 0;
    static uint32_t beaconProcessed = 0;
    static uint32_t beaconDroppedBusy = 0;
    static uint32_t lastCbStatsLog = 0;
    uint32_t _now = millis();
    if (_now - lastCbStatsLog > 5000) {
        Serial.printf("[OINK-CB] eapol tot=%lu proc=%lu drop/busy=%lu | beacon tot=%lu proc=%lu drop/busy=%lu\n",
            eapolTotal, eapolProcessed, eapolDroppedBusy,
            beaconTotal, beaconProcessed, beaconDroppedBusy);
        lastCbStatsLog = _now;
    }
```

И в switch(type):
```
        case WIFI_PKT_MGMT:
            if (frameSubtype == 0x08) {
                beaconTotal++;
                if (NetworkRecon::isBusy()) { beaconDroppedBusy++; break; }
                beaconProcessed++;
                processBeacon(payload, len, rssi);
            }
            break;
        case WIFI_PKT_DATA:
            eapolTotal++;
            eapolProcessed++;
            processDataFrame(payload, len, rssi);
            break;
```

Зачем: даст видимость, дропаются ли EAPOL. Без этого ты гадаешь. После
правок №3+5 drop/busy для eapol должен быть 0.

---

## Правка №7 (диагностика фильтра)

Файл: src/modes/oink.cpp
Функция: OinkMode::processEAPOL(), рядом с shouldStoreHandshakeForStation

Добавить лог до return:
```
    if (!OinkCaptureFilters::shouldStoreHandshakeForStation(stationIsOurs)) {
        Serial.printf("[OINK] DROPPED handshake b=%02X:%02X:%02X:%02X:%02X:%02X s=%02X:%02X:%02X:%02X:%02X:%02X (ours)\n",
            bssid[0],bssid[1],bssid[2],bssid[3],bssid[4],bssid[5],
            station[0],station[1],station[2],station[3],station[4],station[5]);
        return;
    }
```

Зачем: видно, фильтр дропает настоящие хендшейки (если дропает — это и
есть часть причины).

---

## Что НЕ менять

- Не возвращай shrink_to_fit — на fragmented heap ESP32 без исключений это
  abort(). Твоё избегание правильное.
- Не трогай PMF детекцию (MFPC vs MFPR) — у тебя правильнее чем в оригинале.
- Не трогай hashcat 22000 (M1+M2 / M2+M3 fallback) — корректный.

---

## Порядок применения

1. Правки №1 + №2 (reserve). Тест: запусти на 30 минут, следи largest free
   block в serial. Не должен падать ниже 8-10KB.
2. Правки №3 + №5 (CAS drop → spin). Тест: OINK-STATS hsBlk{frag=...}
   должен расти медленнее.
3. Правка №4 (проверка) — обычно уже корректна, перепроверь.
4. Правка №6 (счётчики). Тест: запусти, жди "перестало", посмотри
   drop/busy для eapol — если растёт, CAS-схема ещё дропает.
5. Правка №7 (лог фильтра). Параллельно с №6.

---

## TL;DR

reserve() + убрать CAS-drop — две вещи, которые с высокой вероятностью
вернут стабильную ловлю. Остальное — диагностика и защита.
