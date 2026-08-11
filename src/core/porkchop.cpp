// Porkchop core state machine implementation

#include "porkchop.h"
#include <M5Cardputer.h>
#include "../ui/display.h"
#include "../ui/menu.h"
#include "../ui/settings_menu.h"
#include "../ui/hashes_menu.h"
#include "../ui/badges_menu.h"
#include "../ui/bounty_menu.h"
#include "../ui/coredump_viewer.h"
#include "../ui/diagdata_menu.h"
#include "../ui/flexes_screen.h"
#include "../ui/boar_bros_menu.h"
#include "../ui/tracks_menu.h"
#include "../ui/pwncrack_menu.h"
#include "../ui/unlockables_menu.h"
#include "../ui/sd_format_menu.h"
#include "../ui/files_menu.h"
#include "../piglet/mood.h"
#include "../piglet/avatar.h"
#include "../piglet/wolf.h"
#include "../modes/oink.h"
#include "heap_policy.h"
#include "../modes/do_no_ham.h"
#include "../modes/warhog.h"
#include "../modes/piggy_blues.h"
#include "../modes/spectrum.h"
#include "../modes/pigsync_mode.h"
#include "../modes/bacon.h"
#include "janus_hog.h"
#include "../modes/charging.h"
#include "../modes/pigpass.h"
#include "../modes/micpork.h"
#include "../modes/evilpig.h"
#include "../modes/fruit_run.h"
#include "../modes/ir_pork.h"
#include "../web/xfer_server.h"
#include "../audio/sfx.h"
#include "config.h"
#include "heap_health.h"
#include "xp.h"
#include "sdlog.h"
#include "sd_format.h"
#include "challenges.h"
#include "stress_test.h"
#include "network_recon.h"
#include "wifi_utils.h"
#include <esp_heap_caps.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <WiFi.h>

static const char* modeToString(PorkchopMode mode) {
    switch (mode) {
        case PorkchopMode::IDLE: return "IDLE";
        case PorkchopMode::OINK_MODE: return "OINK";
        case PorkchopMode::DNH_MODE: return "DNH";
        case PorkchopMode::WARHOG_MODE: return "WARHOG";
        case PorkchopMode::PIGGYBLUES_MODE: return "PIGGYBLUES";
        case PorkchopMode::SPECTRUM_MODE: return "SPECTRUM";
        case PorkchopMode::MENU: return "MENU";
        case PorkchopMode::SETTINGS: return "SETTINGS";
        case PorkchopMode::HASHES: return "HASHES";
        case PorkchopMode::PWNCRACK_MODE: return "PWNCRACK";
        case PorkchopMode::BADGES: return "BADGES";
        case PorkchopMode::XFER: return "XFER";
        case PorkchopMode::COREDUMP: return "COREDUMP";
        case PorkchopMode::DIAGDATA: return "DIAGDATA";
        case PorkchopMode::FLEXES: return "FLEXES";
        case PorkchopMode::BOAR_BROS: return "BOAR_BROS";
        case PorkchopMode::TRACKS: return "TRACKS";
        case PorkchopMode::UNLOCKABLES: return "UNLOCKABLES";
        case PorkchopMode::BOUNTY: return "BOUNTY";
        case PorkchopMode::PIGSYNC_DEVICE_SELECT: return "PIGSYNC_DEVICE_SELECT";
        case PorkchopMode::BACON_MODE: return "BACON";
        case PorkchopMode::JANUS_HOG_MODE: return "JANUS_HOG";
        case PorkchopMode::SD_FORMAT: return "SD_FORMAT";
        case PorkchopMode::CHARGING: return "CHARGING";
        case PorkchopMode::PIGPASS_MODE: return "PIGPASS";
        case PorkchopMode::MICPORK_MODE: return "MICPORK";
        case PorkchopMode::EVILPIG_MODE: return "EVILPIG";
        case PorkchopMode::FILES_MODE: return "FILES";
        case PorkchopMode::FRUIT_RUN_MODE: return "FRUITRUN";
        case PorkchopMode::IR_PORK_MODE: return "IRPORK";
        case PorkchopMode::ABOUT: return "ABOUT";
        default: return "UNKNOWN";
    }
}

// Crash-loop guard: count early reboots using RTC memory (survives soft resets).
RTC_DATA_ATTR static uint8_t bootGuardStreak = 0;
static uint32_t bootGuardStartMs = 0;
static const uint8_t BOOT_GUARD_THRESHOLD = 3;
static const uint32_t BOOT_GUARD_WINDOW_MS = 60000;

static PorkchopMode bootModeToPorkchop(BootMode mode) {
    switch (mode) {
        case BootMode::OINK: return PorkchopMode::OINK_MODE;
        case BootMode::DNOHAM: return PorkchopMode::DNH_MODE;
        case BootMode::WARHOG: return PorkchopMode::WARHOG_MODE;
        case BootMode::IDLE:
        default:
            return PorkchopMode::IDLE;
    }
}

static const char* bootModeLabel(BootMode mode) {
    switch (mode) {
        case BootMode::OINK: return "OINK";
        case BootMode::DNOHAM: return "DN0HAM";
        case BootMode::WARHOG: return "WARHOG";
        case BootMode::IDLE:
        default:
            return "IDLE";
    }
}

Porkchop::Porkchop()
    : currentMode(PorkchopMode::IDLE)
    , previousMode(PorkchopMode::IDLE)
    , startTime(0)
    , handshakeCount(0)
    , networkCount(0)
    , deauthCount(0) {
    eventQueue.reserve(MAX_EVENT_QUEUE_SIZE);
    callbacks.reserve(16);
}

static bool isAutoConditionSafe(PorkchopMode mode) {
    switch (mode) {
        case PorkchopMode::IDLE:
        case PorkchopMode::MENU:
        case PorkchopMode::SETTINGS:
        case PorkchopMode::ABOUT:
        case PorkchopMode::BADGES:
        case PorkchopMode::HASHES:   // allow brew while sitting on SCAN DEFERRED
        case PorkchopMode::TRACKS:
        case PorkchopMode::COREDUMP:
        case PorkchopMode::DIAGDATA:
        case PorkchopMode::FLEXES:
        case PorkchopMode::BOAR_BROS:
        case PorkchopMode::UNLOCKABLES:
        case PorkchopMode::BOUNTY:
        case PorkchopMode::SD_FORMAT:
        case PorkchopMode::FILES_MODE:
            return true;
        default:
            return false;
    }
}

static void maybeAutoConditionHeap(PorkchopMode mode) {
    if (!isAutoConditionSafe(mode)) {
        return;
    }
    if (XferServer::isRunning() || XferServer::isConnecting()) {
        return;
    }
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }
    // At Critical pressure (<30KB free), brew needs 35KB transient — would fail anyway
    if (static_cast<uint8_t>(HeapHealth::getPressureLevel()) > HeapPolicy::kMaxPressureLevelForAutoBrew) {
        return;
    }
    if (!HeapHealth::consumeConditionRequest()) {
        return;
    }

    bool wasReconRunning = NetworkRecon::isRunning();
    if (wasReconRunning) {
        NetworkRecon::pause();
    }
    // Small, low-disruption brew to coalesce heap when health drops.
    WiFiUtils::brewHeap(HeapPolicy::kBrewAutoDwellMs, false);
    if (wasReconRunning) {
        NetworkRecon::resume();
    }
}

void Porkchop::init() {
    startTime = millis();
    
    // Initialize background network reconnaissance service
    NetworkRecon::init();
    
    // Initialize XP system
    XP::init();
    
    // Initialize FlexesScreen (buff/debuff system)
    FlexesScreen::init();
    
    // Register level up callback to show popup
    XP::setLevelUpCallback([](uint8_t oldLevel, uint8_t newLevel) {
        Display::showLevelUp(oldLevel, newLevel);
        Avatar::cuteJump();  // Celebratory jump on level up!
        
        // Check if class tier changed (every 5 levels: 6, 11, 16, 21, 26, 31, 36)
        PorkClass oldClass = XP::getClassForLevel(oldLevel);
        PorkClass newClass = XP::getClassForLevel(newLevel);
        if (newClass != oldClass) {
            // Small delay between popups
            delay(500);
            Display::showClassPromotion(
                XP::getClassNameFor(oldClass),
                XP::getClassNameFor(newClass)
            );
        }
    });
    
    // Register default event handlers
    registerCallback(PorkchopEvent::HANDSHAKE_CAPTURED, [this](PorkchopEvent, void*) {
        handshakeCount++;
    });
    
    registerCallback(PorkchopEvent::NETWORK_FOUND, [this](PorkchopEvent, void*) {
        networkCount++;
    });
    
    registerCallback(PorkchopEvent::DEAUTH_SENT, [this](PorkchopEvent, void*) {
        deauthCount++;
    });
    
    // Menu selection handler - items now defined in menu.cpp as static arrays
    Menu::setCallback([this](uint8_t actionId) {
        switch (actionId) {
            case 1: setMode(PorkchopMode::OINK_MODE); break;
            case 2: setMode(PorkchopMode::WARHOG_MODE); break;
            case 3: setMode(PorkchopMode::XFER); break;
            case 4: setMode(PorkchopMode::HASHES); break;
            case 5: setMode(PorkchopMode::SETTINGS); break;
            case 6: setMode(PorkchopMode::ABOUT); break;
            case 7: setMode(PorkchopMode::COREDUMP); break;
            case 8: setMode(PorkchopMode::PIGGYBLUES_MODE); break;
            case 9: setMode(PorkchopMode::BADGES); break;
            case 10: setMode(PorkchopMode::SPECTRUM_MODE); break;
            case 11: setMode(PorkchopMode::FLEXES); break;
            case 12: setMode(PorkchopMode::BOAR_BROS); break;
            case 13: setMode(PorkchopMode::TRACKS); break;
            case 14: setMode(PorkchopMode::DNH_MODE); break;
            case 15: setMode(PorkchopMode::UNLOCKABLES); break;
            case 16: setMode(PorkchopMode::PIGSYNC_DEVICE_SELECT); break;
            case 17: setMode(PorkchopMode::BOUNTY); break;
            case 18: setMode(PorkchopMode::BACON_MODE); break;
            case 19: setMode(PorkchopMode::DIAGDATA); break;
            case 20: setMode(PorkchopMode::SD_FORMAT); break;
            case 21: setMode(PorkchopMode::CHARGING); break;
            case 22: setMode(PorkchopMode::JANUS_HOG_MODE); break;
            case 23: setMode(PorkchopMode::PIGPASS_MODE); break;
            case 24: setMode(PorkchopMode::MICPORK_MODE); break;
            case 25: setMode(PorkchopMode::EVILPIG_MODE); break;
            case 26: setMode(PorkchopMode::FILES_MODE); break;
            case 27: setMode(PorkchopMode::FRUIT_RUN_MODE); break;
            case 28: setMode(PorkchopMode::IR_PORK_MODE); break;
            case 29: Display::showChallenges(); break;  // RANK → D3M4NDS (session trials)
            case 30: setMode(PorkchopMode::PWNCRACK_MODE); break;
        }
    });

    bootGuardStartMs = millis();
    if (bootGuardStreak < 255) {
        bootGuardStreak++;
    }
    bool bootGuardActive = bootGuardStreak >= BOOT_GUARD_THRESHOLD;

    // Fan reliability (1.3 radio stack): OINK after boot warms WiFi/heap so
    // XFER / WPA-SEC / BLUES don't need a manual "OINK bounce" first.
    // Settings → BOOT MODE still wins if user picked DNOHAM/WARHOG/OINK.
    // If BOOT MODE is IDLE (old NVS default), force OINK warm-up anyway.
    BootMode bootMode = Config::personality().bootMode;
    if (bootMode == BootMode::IDLE) {
        bootMode = BootMode::OINK;
    }
    bootModeTarget = bootModeToPorkchop(bootMode);
    if (bootModeTarget != PorkchopMode::IDLE && !bootGuardActive) {
        bootModePending = true;
        bootModeStartMs = millis();
        char buf[36];
        snprintf(buf, sizeof(buf), "BOOT -> %s IN 5S", bootModeLabel(bootMode));
        Display::showToast(buf, 5000);
    } else if (bootModeTarget != PorkchopMode::IDLE && bootGuardActive) {
        Display::showToast("BOOT GUARD - IDLE", 4000);
    }
    
    Avatar::setState(AvatarState::HAPPY);
    
    // SFX::init() already called in setup() for boot sound — don't re-init
    // Boot toast: highlight big new features (not last bugfix)
    Display::showToast(
        "NEW: PWNCRACK  PIGPASS\n"
        "NEW: PIGPASSI=IR\n"
        "NEW: EVILPIG",
        1500
    );
    
    Serial.println("[PORKCHOP] Initialized");
    SDLog::log("PORK", "Initialized - LV%d %s", XP::getLevel(), XP::getTitle());
}

void Porkchop::update() {
    // Update background network reconnaissance (channel hopping, cleanup)
    NetworkRecon::update();
    
    processEvents();
    yield(); // Allow other tasks to run between operations
    handleInput();
    yield(); // Allow other tasks to run between operations
    
    if (bootGuardStreak > 0 && (millis() - bootGuardStartMs >= BOOT_GUARD_WINDOW_MS)) {
        bootGuardStreak = 0;
    }
    if (bootModePending) {
        if (currentMode != PorkchopMode::IDLE) {
            bootModePending = false;
        } else if (millis() - bootModeStartMs >= 5000) {
            bootModePending = false;
            setMode(bootModeTarget);
        }
    }
    updateMode();

    maybeAutoConditionHeap(currentMode);
    
    // Tick non-blocking audio engine
    SFX::update();
    yield(); // Allow other tasks to run between operations
    
    // Process one queued achievement celebration (debounced)
    XP::processAchievementQueue();
    yield(); // Allow other tasks to run between operations
    
    // Stress test injection (if active)
    StressTest::update();
    yield(); // Allow other tasks to run between operations
    
    // Check for session time XP bonuses
    XP::updateSessionTime();
    yield(); // Allow other tasks to run between operations
}

void Porkchop::setMode(PorkchopMode mode) {
    if (mode == currentMode) return;
    
    // Store the mode we're leaving for cleanup
    PorkchopMode oldMode = currentMode;

    Serial.printf("[MODE] EXIT %s free=%u largest=%u\n",
        modeToString(oldMode),
        (unsigned)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    
    // Save "real" modes as previous (not modal menus)
    // Exception: HASHES and TRACKS are saved as previousMode so OINK recovery returns to them
    if (currentMode != PorkchopMode::SETTINGS &&
        currentMode != PorkchopMode::ABOUT &&
        currentMode != PorkchopMode::BADGES &&
        currentMode != PorkchopMode::MENU &&
        currentMode != PorkchopMode::XFER &&
        currentMode != PorkchopMode::COREDUMP &&
        currentMode != PorkchopMode::DIAGDATA &&
        currentMode != PorkchopMode::FLEXES &&
        currentMode != PorkchopMode::BOAR_BROS &&
        currentMode != PorkchopMode::BOUNTY &&
        currentMode != PorkchopMode::PIGSYNC_DEVICE_SELECT &&
        currentMode != PorkchopMode::UNLOCKABLES &&
        currentMode != PorkchopMode::SD_FORMAT &&
        currentMode != PorkchopMode::FILES_MODE) {
        previousMode = currentMode;
    }
    // ALSO save HASHES and TRACKS as return points from OINK recovery
    if (currentMode == PorkchopMode::HASHES ||
        currentMode == PorkchopMode::TRACKS) {
        previousMode = currentMode;
    }
    currentMode = mode;

    Serial.printf("[MODE] ENTER %s free=%u largest=%u\n",
        modeToString(currentMode),
        (unsigned)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    // Park pig scene during CPU/heap-heavy modes so mood/weather/wolf
    // don't compete with PBKDF2, SoftAP, or TLS xfer.
    auto isSceneHeavy = [](PorkchopMode m) {
        return m == PorkchopMode::PIGPASS_MODE ||
               m == PorkchopMode::EVILPIG_MODE ||
               m == PorkchopMode::XFER;
    };
    if (isSceneHeavy(mode) && !isSceneHeavy(oldMode)) {
        Avatar::suspendScene();
    } else if (!isSceneHeavy(mode) && isSceneHeavy(oldMode)) {
        Avatar::resumeScene();
    }

    // Wolf only on free roam (IDLE) or Fruit Run — never during O/W/B/D/I/…
    auto wolfAllowed = [](PorkchopMode m) {
        return m == PorkchopMode::IDLE || m == PorkchopMode::FRUIT_RUN_MODE;
    };
    if (!wolfAllowed(mode)) {
        Wolf::reset();
        Wolf::setAutoSpawn(false);
    } else if (mode == PorkchopMode::IDLE) {
        // Fruit Run manages its own spawn; IDLE gets ambient visitors back
        Wolf::setAutoSpawn(true);
    }
    
    // Cleanup the mode we're actually leaving (oldMode), not previousMode
    switch (oldMode) {
        case PorkchopMode::OINK_MODE:
            OinkMode::stop();
            break;
        case PorkchopMode::DNH_MODE:
            DoNoHamMode::stop();
            break;
        case PorkchopMode::WARHOG_MODE:
            WarhogMode::stop();
            break;
        case PorkchopMode::PIGGYBLUES_MODE:
            PiggyBluesMode::stop();
            break;
        case PorkchopMode::SPECTRUM_MODE:
            SpectrumMode::stop();
            break;
        case PorkchopMode::MENU:
            Menu::hide();
            break;
        case PorkchopMode::SETTINGS:
            SettingsMenu::hide();
            break;
        case PorkchopMode::HASHES:
            HashesMenu::hide();
            break;
        case PorkchopMode::PWNCRACK_MODE:
            PwncrackMenu::hide();
            break;
        case PorkchopMode::BADGES:
            BadgesMenu::hide();
            break;
        case PorkchopMode::XFER:
            XferServer::stop();
            // Restart NetworkRecon after XFER to resume background scanning
            NetworkRecon::start();
            break;
        case PorkchopMode::COREDUMP:
            CoreDumpViewer::hide();
            break;
        case PorkchopMode::DIAGDATA:
            DiagDataMenu::hide();
            break;
        case PorkchopMode::SD_FORMAT:
            SdFormatMenu::hide();
            break;
        case PorkchopMode::FILES_MODE:
            FilesMenu::hide();
            break;
        case PorkchopMode::FLEXES:
            FlexesScreen::hide();
            break;
        case PorkchopMode::BOAR_BROS:
            BoarBrosMenu::hide();
            break;
        case PorkchopMode::TRACKS:
            TracksMenu::hide();
            break;
        case PorkchopMode::UNLOCKABLES:
            UnlockablesMenu::hide();
            break;
        case PorkchopMode::BOUNTY:
            BountyMenu::hide();
            break;
        case PorkchopMode::PIGSYNC_DEVICE_SELECT:
            PigSyncMode::stopDiscovery();
            PigSyncMode::stop();
            break;
        case PorkchopMode::BACON_MODE:
            BaconMode::stop();
            break;
        case PorkchopMode::JANUS_HOG_MODE:
            // Thin viewer mode — nothing to stop (service runs independently)
            break;
        case PorkchopMode::CHARGING:
            ChargingMode::stop();
            // CHARGING explicitly shuts down JanusHog to save power.
            // Re-init it on exit so JANUS HOG returns automatically if enabled.
            if (JanusHog::isEnabled() && JanusHog::getState() == C5State::OFF) {
                JanusHog::init();
            }
            break;
        case PorkchopMode::PIGPASS_MODE:
            PigpassMode::stop();
            break;
        case PorkchopMode::MICPORK_MODE:
            MicPorkMode::stop();
            break;
        case PorkchopMode::EVILPIG_MODE:
            EvilPigMode::stop();
            break;
        case PorkchopMode::FRUIT_RUN_MODE:
            FruitRunMode::stop();
            break;
        case PorkchopMode::IR_PORK_MODE:
            IrPorkMode::stop();
            break;
        default:
            break;
    }
    
    // Mode transition sound feedback
    // Exit sound for "real" modes (not sub-menus returning to parent)
    bool isSubMenu = (mode == PorkchopMode::IDLE || mode == PorkchopMode::MENU);
    bool wasSubMenu = (oldMode == PorkchopMode::MENU || oldMode == PorkchopMode::SETTINGS ||
                       oldMode == PorkchopMode::ABOUT || oldMode == PorkchopMode::BADGES ||
                       oldMode == PorkchopMode::HASHES || oldMode == PorkchopMode::TRACKS ||
                       oldMode == PorkchopMode::FLEXES || oldMode == PorkchopMode::BOAR_BROS ||
                       oldMode == PorkchopMode::UNLOCKABLES || oldMode == PorkchopMode::BOUNTY ||
                       oldMode == PorkchopMode::FILES_MODE || oldMode == PorkchopMode::COREDUMP ||
                       oldMode == PorkchopMode::SD_FORMAT);
    if (wasSubMenu && isSubMenu) {
        SFX::play(SFX::BACK_NAV);
    } else if (!isSubMenu) {
        SFX::play(SFX::MODE_ENTER);
    }

    // Init new mode
    switch (currentMode) {
        case PorkchopMode::IDLE:
            Avatar::setState(AvatarState::NEUTRAL);
            Mood::onIdle();
            XP::save();  // Save XP when returning to idle
            SDLog::log("PORK", "Mode: IDLE");
            break;
        case PorkchopMode::OINK_MODE:
            Avatar::setState(AvatarState::HUNTING);
            Display::notify(NoticeKind::STATUS, "PROPER MAD ONE INNIT", 5000, NoticeChannel::TOP_BAR);
            SDLog::log("PORK", "Mode: OINK");
            OinkMode::start();
            break;
        case PorkchopMode::DNH_MODE:
            Avatar::setState(AvatarState::NEUTRAL);  // Calm, passive state
            SDLog::log("PORK", "Mode: DO NO HAM");
            DoNoHamMode::start();
            break;
        case PorkchopMode::WARHOG_MODE:
            Avatar::setState(AvatarState::EXCITED);
            Display::notify(NoticeKind::STATUS, "SNIFFING THE AIR", 5000, NoticeChannel::TOP_BAR);
            SDLog::log("PORK", "Mode: WARHOG");
            // Disable ML/Enhanced features for heap savings
            {
                auto mlCfg = Config::ml();
                mlCfg.enabled = false;
                mlCfg.collectionMode = MLCollectionMode::BASIC;
                Config::setML(mlCfg);
            }
            WarhogMode::start();
            break;
        case PorkchopMode::PIGGYBLUES_MODE:
            Avatar::setState(AvatarState::ANGRY);
            SDLog::log("PORK", "Mode: PIGGYBLUES");
            PiggyBluesMode::start();
            // If user aborted warning dialog, return to menu
            if (!PiggyBluesMode::isRunning()) {
                currentMode = PorkchopMode::MENU;
                Menu::show();
            }
            break;
        case PorkchopMode::SPECTRUM_MODE:
            Avatar::setState(AvatarState::HUNTING);
            SDLog::log("PORK", "Mode: SPECTRUM");
            SpectrumMode::start();
            break;
        case PorkchopMode::MENU:
            Menu::show();
            break;
        case PorkchopMode::SETTINGS:
            SettingsMenu::show();
            break;
        case PorkchopMode::HASHES:
            HashesMenu::show();
            break;
        case PorkchopMode::PWNCRACK_MODE:
            PwncrackMenu::show();
            break;
        case PorkchopMode::BADGES:
            BadgesMenu::show();
            break;
        case PorkchopMode::XFER:
            // Stop NetworkRecon and free its ~19KB network vector — XFER doesn't use it
            NetworkRecon::stop();
            NetworkRecon::freeNetworks();
            Avatar::setState(AvatarState::HAPPY);
            XferServer::start(Config::wifi().otaSSID, Config::wifi().otaPassword);
            break;
        case PorkchopMode::COREDUMP:
            CoreDumpViewer::show();
            break;
        case PorkchopMode::DIAGDATA:
            DiagDataMenu::show();
            break;
        case PorkchopMode::SD_FORMAT:
            SdFormatMenu::show();
            break;
        case PorkchopMode::FILES_MODE:
            SDLog::log("PORK", "Mode: FILES");
            FilesMenu::show();
            break;
        case PorkchopMode::FLEXES:
            FlexesScreen::show();
            break;
        case PorkchopMode::BOAR_BROS:
            BoarBrosMenu::show();
            break;
        case PorkchopMode::TRACKS:
            TracksMenu::show();
            break;
        case PorkchopMode::UNLOCKABLES:
            UnlockablesMenu::show();
            break;
        case PorkchopMode::BOUNTY:
            BountyMenu::show();
            break;
        case PorkchopMode::PIGSYNC_DEVICE_SELECT:
            Avatar::setState(AvatarState::EXCITED);
            SDLog::log("PORK", "Mode: PIGSYNC Device Select");
            PigSyncMode::start();
            PigSyncMode::startDiscovery();
            break;
        case PorkchopMode::BACON_MODE:
            Avatar::setState(AvatarState::HAPPY);
            SDLog::log("PORK", "Mode: BACON");
            BaconMode::init();
            BaconMode::start();
            break;
        case PorkchopMode::MICPORK_MODE:
            Avatar::setState(AvatarState::EXCITED);
            SDLog::log("PORK", "Mode: MICPORK");
            MicPorkMode::init();
            MicPorkMode::start();
            break;
        case PorkchopMode::FRUIT_RUN_MODE:
            Avatar::setState(AvatarState::EXCITED);
            SDLog::log("PORK", "Mode: FRUIT_RUN");
            FruitRunMode::init();
            FruitRunMode::start();
            break;
        case PorkchopMode::IR_PORK_MODE:
            Avatar::setState(AvatarState::HUNTING);
            SDLog::log("PORK", "Mode: IR_PORK");
            IrPorkMode::init();
            IrPorkMode::start();
            break;
        case PorkchopMode::EVILPIG_MODE:
            SDLog::log("PORK", "Mode: EVILPIG");
            EvilPigMode::init();
            EvilPigMode::start();
            // User may cancel confirm dialog
            if (!EvilPigMode::isRunning()) {
                currentMode = PorkchopMode::MENU;
                Menu::show();
            }
            break;
        case PorkchopMode::JANUS_HOG_MODE:
            Avatar::setState(AvatarState::EXCITED);
            SDLog::log("PORK", "Mode: JANUS_HOG");
            if (JanusHog::isConnected()) {
                // Trigger initial scan when entering mode
                if (JanusHog::getScanCount() == 0) {
                    JanusHog::requestScan();
                }
            } else if (JanusHog::isEnabled()) {
                Display::notify(NoticeKind::STATUS, "C5 CONNECTING...", 2000, NoticeChannel::TOP_BAR);
            } else {
                Display::notify(NoticeKind::STATUS, "C5 DISABLED", 2000, NoticeChannel::TOP_BAR);
            }
            break;
        case PorkchopMode::ABOUT:
            Display::resetAboutState();
            break;
        case PorkchopMode::CHARGING:
            SDLog::log("PORK", "Mode: CHARGING");
            JanusHog::shutdown();  // Stop C5 to save power
            ChargingMode::start();
            break;
        case PorkchopMode::PIGPASS_MODE:
            Avatar::setState(AvatarState::HUNTING);
            SDLog::log("PORK", "Mode: PIGPASS");
            PigpassMode::start();
            break;
        default:
            break;
    }
    
    postEvent(PorkchopEvent::MODE_CHANGE, nullptr);
}

void Porkchop::postEvent(PorkchopEvent event, void* data) {
    // Prevent event queue overflow that could cause heap fragmentation
    if (eventQueue.size() >= MAX_EVENT_QUEUE_SIZE) {
        // Drop oldest event to maintain queue size
        eventQueue.erase(eventQueue.begin());
    }
    eventQueue.push_back({event, data});
}

void Porkchop::registerCallback(PorkchopEvent event, EventCallback callback) {
    // Prevent duplicate callbacks for the same event to avoid multiple executions
    // Note: We can't reliably compare std::function objects, so we just ensure each event
    // type has only one callback by replacing any existing one
    for (auto& pair : callbacks) {
        if (pair.first == event) {
            pair.second = callback; // Replace existing callback
            return;
        }
    }
    // Add bounds checking to prevent unlimited growth
    if (callbacks.size() >= MAX_EVENT_QUEUE_SIZE) {
        // Remove the oldest callback if we're at capacity
        callbacks.erase(callbacks.begin());
    }
    callbacks.push_back({event, callback});
}

void Porkchop::processEvents() {
    // Process events with bounds checking and yield for WDT safety
    // NOTE: All postEvent() callers pass nullptr for data — no ownership to track.
    size_t processed = 0;
    const size_t MAX_EVENTS_PER_UPDATE = 16; // Limit events processed per update to prevent WDT

    // Index-based loop to avoid iterator invalidation from erase()
    size_t i = 0;
    while (i < eventQueue.size() && processed < MAX_EVENTS_PER_UPDATE) {
        const auto item = eventQueue[i];  // Copy, not reference: callbacks may push_back() which reallocates

        for (const auto& cb : callbacks) {
            if (cb.first == item.event) {
                cb.second(item.event, item.data);

                if (++processed % 4 == 0) {
                    yield();
                }
            }
        }
        i++;
    }

    // Erase all processed events in one operation after the loop
    if (i >= eventQueue.size()) {
        eventQueue.clear();
    } else {
        eventQueue.erase(eventQueue.begin(), eventQueue.begin() + i);
    }
}

void Porkchop::handleInput() {
    // G0 button (GPIO0 on top side) - configurable action
    static bool g0WasPressed = false;
    bool g0Pressed = (digitalRead(0) == LOW);  // G0 is active LOW

    if (g0Pressed && !g0WasPressed) {
        G0Action g0Action = Config::personality().g0Action;
        if (g0Action != G0Action::SCREEN_TOGGLE) {
            Display::resetDimTimer();  // Wake screen on G0
        }
        Serial.printf("[PORKCHOP] G0 pressed! Current mode: %d\n", (int)currentMode);
        switch (g0Action) {
            case G0Action::SCREEN_TOGGLE:
                Display::toggleScreenPower();
                break;
            case G0Action::OINK:
                setMode(PorkchopMode::OINK_MODE);
                break;
            case G0Action::DNOHAM:
                setMode(PorkchopMode::DNH_MODE);
                break;
            case G0Action::SPECTRUM:
                setMode(PorkchopMode::SPECTRUM_MODE);
                break;
            case G0Action::PIGSYNC:
                setMode(PorkchopMode::PIGSYNC_DEVICE_SELECT);
                break;
            case G0Action::IDLE:
                setMode(PorkchopMode::IDLE);
                break;
            default:
                break;
        }
        g0WasPressed = true;
        return;
    }
    if (!g0Pressed) {
        g0WasPressed = false;
    }

    // IDLE free roam: poll EVERY frame so hold-to-walk works (before isChange gate).
    //   ,  left hold    /  right hold  (world scrolls with pig — like B/Piggy Blues)
    //   ;  jump         SPACE attack-hop
    //   .  sit hold
    // Wolf bite locks all controls for 10s (play-dead).
    if (currentMode == PorkchopMode::IDLE) {
        bool left  = M5Cardputer.Keyboard.isKeyPressed(',');
        bool right = M5Cardputer.Keyboard.isKeyPressed('/');
        bool jumpKey = M5Cardputer.Keyboard.isKeyPressed(';');
        bool attackKey = M5Cardputer.Keyboard.isKeyPressed(' ');
        bool sitKey = M5Cardputer.Keyboard.isKeyPressed('.');

        static bool idleJumpWas = false;
        static bool idleAttackWas = false;
        static uint32_t idleMoveMs = 0;
        bool jumpEdge = jumpKey && !idleJumpWas;
        bool attackEdge = attackKey && !idleAttackWas;
        idleJumpWas = jumpKey;
        idleAttackWas = attackKey;

        const bool locked = Avatar::isControlLocked() || Avatar::isPlayDead();

        if (!locked) {
            Avatar::setSitting(sitKey && !left && !right && !jumpKey && !attackKey);
        }

        uint32_t nowMove = millis();
        // Fixed walk tick — scroll rate is separate (SCENE → SCROLL SPD)
        uint32_t tickMs = 18;
        if (nowMove - idleMoveMs >= tickMs) {
            idleMoveMs = nowMove;
            if (locked) {
                Avatar::playerWalkHold(0);
            } else if (left && !right) {
                Avatar::playerWalkHold(-1);
            } else if (right && !left) {
                Avatar::playerWalkHold(+1);
            } else {
                Avatar::playerWalkHold(0);
            }
        }
        if (!locked && jumpEdge && !Avatar::isJumping() && !Avatar::isAttackHopping()) {
            Avatar::setPlayDead(false);
            Avatar::setSitting(false);
            Avatar::cuteJump();
            Avatar::setState(AvatarState::HAPPY);
        }
        // SPACE = attack hop (scares wolf if close — handled in Avatar::draw)
        if (!locked && attackEdge && !Avatar::isAttackHopping() && !Avatar::isJumping()) {
            Avatar::setPlayDead(false);
            Avatar::setSitting(false);
            Avatar::attackHop();
            Avatar::setState(AvatarState::HUNTING);
        }
    }
    
    if (!M5Cardputer.Keyboard.isChange()) return;
    
    // Any keyboard input resets the screen dim timer
    Display::resetDimTimer();
    
    auto keys = M5Cardputer.Keyboard.keysState();
    // ESC maps to the key above Tab (shares ` / ~)
    bool escPressed = M5Cardputer.Keyboard.isKeyPressed('`');

    // ESC to return to IDLE from any active mode
    if (escPressed && currentMode != PorkchopMode::IDLE) {
        setMode(PorkchopMode::IDLE);
        return;
    }
    
    // In MENU mode, let Menu::handleInput() process navigation keys
    if (currentMode == PorkchopMode::MENU) {
        // Do NOT return here - let Menu::update() handle navigation
        // But we already consumed isChange(), so Menu won't see it
        // Instead, call Menu::update() directly here
        Menu::update();
        yield(); // Allow other tasks to run during menu updates
        return;
    }
    
    // In SETTINGS mode, let SettingsMenu handle everything
    if (currentMode == PorkchopMode::SETTINGS) {
        // Check if settings wants to exit
        if (SettingsMenu::shouldExit()) {
            SettingsMenu::clearExit();
            SettingsMenu::hide();
            setMode(PorkchopMode::MENU);
        }
        return;
    }

    // In PIGSYNC_DEVICE_SELECT mode, handle navigation and channel switching
    if (currentMode == PorkchopMode::PIGSYNC_DEVICE_SELECT) {
        uint8_t deviceCount = PigSyncMode::getDeviceCount();

        // Handle device navigation (up/down) - only if devices exist
        if (deviceCount > 0) {
            if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                // Up arrow - select previous device
                PigSyncMode::selectDevice(PigSyncMode::getSelectedIndex() > 0 ?
                    PigSyncMode::getSelectedIndex() - 1 : deviceCount - 1);
            }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                // Down arrow - select next device
                PigSyncMode::selectDevice((PigSyncMode::getSelectedIndex() + 1) % deviceCount);
            }
        }

        // Enter to connect to selected device
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER) && PigSyncMode::getDeviceCount() > 0) {
            uint8_t selectedIdx = PigSyncMode::getSelectedIndex();
            if (selectedIdx < PigSyncMode::getDeviceCount()) {
                PigSyncMode::connectTo(selectedIdx);
            }
        }

        // A to abort sync (when connected)
        if (PigSyncMode::isConnected() && M5Cardputer.Keyboard.isKeyPressed('a')) {
            if (PigSyncMode::isSyncing()) {
                PigSyncMode::abortSync();
            }
        }

        // D to disconnect (when connected)
        if (PigSyncMode::isConnected() && M5Cardputer.Keyboard.isKeyPressed('d')) {
            PigSyncMode::disconnect();
        }

        // R to rescan (when not connected)
        if (!PigSyncMode::isConnected() && M5Cardputer.Keyboard.isKeyPressed('r')) {
            PigSyncMode::startScan();
        }

        return; // Consume input for PIGSYNC_DEVICE_SELECT
    }
    
    // Backtick opens menu from IDLE (kept out of back/exit flow)
    if (currentMode == PorkchopMode::IDLE &&
        M5Cardputer.Keyboard.isKeyPressed('`')) {
        setMode(PorkchopMode::MENU);
        return;
    }
    
    // Screenshot with 0 key (global, works in any mode). P is reserved for PigPass.
    if (M5Cardputer.Keyboard.isKeyPressed('0')) {
        if (!Display::isSnapping()) {
            Display::takeScreenshot();
        }
        return;
    }

    // Avatar animation lab (SETTINGS → ANIM TEST). Only while IDLE.
    // - previous demo, = next demo. Shows name in toast.
    if (currentMode == PorkchopMode::IDLE && Config::personality().animTest) {
        bool prev = M5Cardputer.Keyboard.isKeyPressed('-');
        bool next = M5Cardputer.Keyboard.isKeyPressed('=');
        if (prev || next) {
            // Demo list: emotions + one-shot FX
            static const char* const kAnimNames[] = {
                "NEUTRAL",
                "HAPPY",
                "EXCITED",
                "HUNTING",
                "SLEEPY",
                "SAD",
                "ANGRY",
                "BLINK",
                "SNIFF",
                "JUMP",
                "PERK UP",
                "FLINCH",
                "SPIN",
                "PAW SCRATCH",
                "TAIL WIGGLE",
                "SPARKLES",
                "ATTACK HOP",
                "WAVE IN",
                "WAVE OUT",
                "FACE LEFT",
                "FACE RIGHT",
                "TREE ON",
                "TREE OFF",
                "WOLF",
                "SIT",
                "PLAY DEAD",
                "STAND",
            };
            static const uint8_t kAnimCount =
                (uint8_t)(sizeof(kAnimNames) / sizeof(kAnimNames[0]));
            static int8_t s_animIdx = 0;

            if (next) {
                s_animIdx = (int8_t)((s_animIdx + 1) % kAnimCount);
            } else {
                s_animIdx = (int8_t)((s_animIdx - 1 + kAnimCount) % kAnimCount);
            }

            // Reset one-shot FX that stick
            Avatar::setAttackShake(false, false);
            Avatar::setThunderFlash(false);
            Avatar::setMicDance(0.0f);
            Avatar::waveRipple(WaveMode::NONE, 0);

            switch (s_animIdx) {
                case 0: Avatar::setState(AvatarState::NEUTRAL); break;
                case 1: Avatar::setState(AvatarState::HAPPY); break;
                case 2: Avatar::setState(AvatarState::EXCITED); break;
                case 3: Avatar::setState(AvatarState::HUNTING); break;
                case 4: Avatar::setState(AvatarState::SLEEPY); break;
                case 5: Avatar::setState(AvatarState::SAD); break;
                case 6: Avatar::setState(AvatarState::ANGRY); break;
                case 7:
                    Avatar::setState(AvatarState::HAPPY);
                    Avatar::blink();
                    break;
                case 8:
                    Avatar::setState(AvatarState::HAPPY);
                    Avatar::sniff();
                    break;
                case 9:
                    Avatar::setState(AvatarState::EXCITED);
                    Avatar::cuteJump();
                    break;
                case 10:
                    Avatar::setState(AvatarState::HAPPY);
                    Avatar::perkUp();
                    break;
                case 11:
                    Avatar::setState(AvatarState::SAD);
                    Avatar::flinch();
                    break;
                case 12:
                    Avatar::setState(AvatarState::EXCITED);
                    Avatar::spin();
                    break;
                case 13:
                    Avatar::setState(AvatarState::NEUTRAL);
                    Avatar::pawScratch();
                    break;
                case 14:
                    Avatar::setState(AvatarState::HAPPY);
                    Avatar::triggerTailWiggle();
                    break;
                case 15:
                    Avatar::setState(AvatarState::EXCITED);
                    Avatar::triggerSparkles(8);
                    break;
                case 16:
                    Avatar::setState(AvatarState::HUNTING);
                    Avatar::attackHop();
                    break;
                case 17:
                    Avatar::setState(AvatarState::HUNTING);
                    Avatar::waveRipple(WaveMode::INCOMING, 4);
                    break;
                case 18:
                    Avatar::setState(AvatarState::ANGRY);
                    Avatar::waveRipple(WaveMode::OUTGOING, 4);
                    break;
                case 19:
                    Avatar::setState(AvatarState::NEUTRAL);
                    Avatar::setFacingLeft();
                    break;
                case 20:
                    Avatar::setState(AvatarState::NEUTRAL);
                    Avatar::setFacingRight();
                    break;
                case 21:
                    Avatar::setState(AvatarState::HAPPY);
                    Avatar::showTree(5);
                    break;
                case 22:
                    Avatar::setState(AvatarState::NEUTRAL);
                    Avatar::hideTree();
                    break;
                case 23:
                    // Force wolf visit for testing (own module: wolf.cpp)
                    Avatar::setPlayDead(false);
                    Avatar::setSitting(false);
                    Avatar::setState(AvatarState::EXCITED);
                    Wolf::spawnNow();
                    break;
                case 24:
                    Avatar::setPlayDead(false);
                    Avatar::setSitting(true);
                    Avatar::setState(AvatarState::HAPPY);
                    break;
                case 25:
                    Avatar::setSitting(false);
                    Avatar::setPlayDead(true);
                    break;
                case 26:
                    Avatar::setSitting(false);
                    Avatar::setPlayDead(false);
                    Avatar::setState(AvatarState::NEUTRAL);
                    break;
                default:
                    break;
            }

            char msg[36];
            snprintf(msg, sizeof(msg), "ANIM %d/%u  %s",
                     (int)(s_animIdx + 1), (unsigned)kAnimCount,
                     kAnimNames[s_animIdx]);
            Display::showToast(msg, 1200);
            return;
        }
    }
    
    // T key stress test cycle disabled
    
    // Enter key in About mode - easter egg
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
        if (currentMode == PorkchopMode::ABOUT) {
            Display::onAboutEnterPressed();
            return;
        }
    }
    
    // Mode shortcuts when in IDLE
    // Prefer isKeyPressed() for hotkeys — keys.word is unreliable on Cardputer ADV
    // for some matrix keys (notably M).
    if (currentMode == PorkchopMode::IDLE) {
        if (M5Cardputer.Keyboard.isKeyPressed('m') ||
            M5Cardputer.Keyboard.isKeyPressed('M')) {
            setMode(PorkchopMode::MICPORK_MODE);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('e') ||
            M5Cardputer.Keyboard.isKeyPressed('E')) {
            setMode(PorkchopMode::EVILPIG_MODE);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('o') ||
            M5Cardputer.Keyboard.isKeyPressed('O')) {
            setMode(PorkchopMode::OINK_MODE);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('w') ||
            M5Cardputer.Keyboard.isKeyPressed('W')) {
            setMode(PorkchopMode::WARHOG_MODE);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('b') ||
            M5Cardputer.Keyboard.isKeyPressed('B')) {
            setMode(PorkchopMode::PIGGYBLUES_MODE);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('h') ||
            M5Cardputer.Keyboard.isKeyPressed('H')) {
            setMode(PorkchopMode::SPECTRUM_MODE);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('s') ||
            M5Cardputer.Keyboard.isKeyPressed('S')) {
            setMode(PorkchopMode::FLEXES);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('t') ||
            M5Cardputer.Keyboard.isKeyPressed('T')) {
            setMode(PorkchopMode::SETTINGS);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('d') ||
            M5Cardputer.Keyboard.isKeyPressed('D')) {
            setMode(PorkchopMode::DNH_MODE);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('f') ||
            M5Cardputer.Keyboard.isKeyPressed('F')) {
            setMode(PorkchopMode::XFER);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('c') ||
            M5Cardputer.Keyboard.isKeyPressed('C')) {
            setMode(PorkchopMode::CHARGING);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('p') ||
            M5Cardputer.Keyboard.isKeyPressed('P')) {
            setMode(PorkchopMode::PIGPASS_MODE);
            return;
        }
        // G — Fruit Run mini-game (jump + collect fallen fruit)
        if (M5Cardputer.Keyboard.isKeyPressed('g') ||
            M5Cardputer.Keyboard.isKeyPressed('G')) {
            setMode(PorkchopMode::FRUIT_RUN_MODE);
            return;
        }
        // I — IR PORK blaster (power brute + custom files)
        if (M5Cardputer.Keyboard.isKeyPressed('i') ||
            M5Cardputer.Keyboard.isKeyPressed('I')) {
            setMode(PorkchopMode::IR_PORK_MODE);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('1')) {
            Display::showChallenges();
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('2')) {
            setMode(PorkchopMode::PIGSYNC_DEVICE_SELECT);
            return;
        }
        // Fallback: also scan keys.word for any missed labels
        for (auto c : keys.word) {
            if (c == 'm' || c == 'M') {
                setMode(PorkchopMode::MICPORK_MODE);
                break;
            }
        }
        yield();
    }
    
    // OINK mode - B to exclude network
    if (currentMode == PorkchopMode::OINK_MODE) {
        // B key - add selected network to BOAR BROS exclusion list
        static bool bWasPressed = false;
        bool bPressed = M5Cardputer.Keyboard.isKeyPressed('b') || M5Cardputer.Keyboard.isKeyPressed('B');
        if (bPressed && !bWasPressed) {
            int idx = OinkMode::getSelectionIndex();
            if (OinkMode::excludeNetwork(idx)) {
                Display::showToast("BOAR BRO ADDED!");
                delay(500);
                OinkMode::moveSelectionDown();
            } else {
                Display::showToast("ALREADY A BRO");
                delay(500);
            }
        }
        bWasPressed = bPressed;
        
        // D key - switch to DO NO HAM mode (seamless mode switch)
        static bool dWasPressed_oink = false;
        bool dPressed = M5Cardputer.Keyboard.isKeyPressed('d') || M5Cardputer.Keyboard.isKeyPressed('D');
        if (dPressed && !dWasPressed_oink) {
            // Track passive time for achievements
            SessionStats& sess = const_cast<SessionStats&>(XP::getSession());
            sess.passiveTimeStart = millis();
            
            // Show toast before mode switch (loading screen)
            Display::notify(NoticeKind::STATUS, "IRIE VIBES ONLY NOW", 0, NoticeChannel::TOP_BAR);
            delay(800);
            
            // Seamless switch to DNH mode
            setMode(PorkchopMode::DNH_MODE);
            return;  // Prevent fall-through to DNH block this frame
        }
        dWasPressed_oink = dPressed;
    }
    
    // DNH mode - O key to switch back to OINK
    if (currentMode == PorkchopMode::DNH_MODE) {
        // O key - switch back to OINK mode (seamless mode switch)
        static bool oWasPressed_dnh = false;
        bool oPressed = M5Cardputer.Keyboard.isKeyPressed('o') || M5Cardputer.Keyboard.isKeyPressed('O');
        if (oPressed && !oWasPressed_dnh) {
            // Clear passive time tracking
            SessionStats& sess = const_cast<SessionStats&>(XP::getSession());
            sess.passiveTimeStart = 0;
            
            // Show toast before mode switch (loading screen)
            Display::notify(NoticeKind::STATUS, "PROPER MAD ONE INNIT", 0, NoticeChannel::TOP_BAR);
            delay(800);
            
            // Seamless switch to OINK mode
            setMode(PorkchopMode::OINK_MODE);
            return;  // Prevent any subsequent key handling this frame
        }
        oWasPressed_dnh = oPressed;
    }
    
    // WARHOG mode - use ESC to return to idle
    if (currentMode == PorkchopMode::WARHOG_MODE) {
        // no-op: ESC handled globally
    }
    
    // PIGGYBLUES mode - use ESC to return to idle
    if (currentMode == PorkchopMode::PIGGYBLUES_MODE) {
        // no-op: ESC handled globally
    }
    
    
    // SPECTRUM mode - ESC returns to idle globally
    // If monitoring a network, Spectrum handles its own keys
    if (currentMode == PorkchopMode::SPECTRUM_MODE) {
        // no-op: ESC handled globally
    }
    
    // XFER mode - use ESC to return to idle
    if (currentMode == PorkchopMode::XFER) {
        // no-op: ESC handled globally
    }
    
    yield(); // Allow other tasks to run after processing input
}

void Porkchop::updateMode() {
    switch (currentMode) {
        case PorkchopMode::OINK_MODE:
            OinkMode::update();
            break;
        case PorkchopMode::DNH_MODE:
            DoNoHamMode::update();
            break;
        case PorkchopMode::WARHOG_MODE:
            WarhogMode::update();
            break;
        case PorkchopMode::PIGGYBLUES_MODE:
            PiggyBluesMode::update();
            break;
        case PorkchopMode::SPECTRUM_MODE:
            SpectrumMode::update();
            break;
        case PorkchopMode::BACON_MODE:
            BaconMode::update();
            // Check if user exited
            if (!BaconMode::isRunning()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::MICPORK_MODE:
            MicPorkMode::update();
            if (!MicPorkMode::isRunning()) {
                setMode(PorkchopMode::IDLE);
            }
            break;
        case PorkchopMode::FRUIT_RUN_MODE:
            FruitRunMode::update();
            if (!FruitRunMode::isRunning()) {
                setMode(PorkchopMode::IDLE);
            }
            break;
        case PorkchopMode::IR_PORK_MODE:
            IrPorkMode::update();
            if (!IrPorkMode::isRunning()) {
                setMode(PorkchopMode::IDLE);
            }
            break;
        case PorkchopMode::EVILPIG_MODE:
            EvilPigMode::update();
            if (!EvilPigMode::isRunning()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::HASHES:
            HashesMenu::update();
            if (!HashesMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::PWNCRACK_MODE:
            PwncrackMenu::update();
            if (!PwncrackMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::BADGES:
            BadgesMenu::update();
            if (!BadgesMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::XFER:
            XferServer::update();
            break;
        case PorkchopMode::COREDUMP:
            CoreDumpViewer::update();
            if (!CoreDumpViewer::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::DIAGDATA:
            DiagDataMenu::update();
            if (!DiagDataMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::SD_FORMAT:
            SdFormatMenu::update();
            if (!SdFormatMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::FILES_MODE:
            FilesMenu::update();
            if (!FilesMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::FLEXES:
            FlexesScreen::update();
            if (!FlexesScreen::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::BOAR_BROS:
            BoarBrosMenu::update();
            if (!BoarBrosMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::TRACKS:
            TracksMenu::update();
            if (!TracksMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::UNLOCKABLES:
            UnlockablesMenu::update();
            if (!UnlockablesMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::BOUNTY:
            BountyMenu::update();
            if (!BountyMenu::isActive()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::PIGSYNC_DEVICE_SELECT:
            // Update PigSync discovery process (includes dialogue phases)
            PigSyncMode::update();
            // Stay in device select mode for terminal display
            if (!PigSyncMode::isRunning()) {
                // User exited, go back to menu
                setMode(PorkchopMode::MENU);
            }
            break;
        case PorkchopMode::JANUS_HOG_MODE: {
            // Keyboard handling for JANUS HOG viewer (with debounce)
            static bool c5KeyWasPressed = false;
            bool c5AnyPressed = M5Cardputer.Keyboard.isPressed();
            if (!c5AnyPressed) {
                c5KeyWasPressed = false;
                break;
            }
            if (c5KeyWasPressed) break;
            c5KeyWasPressed = true;

            if (M5Cardputer.Keyboard.isKeyPressed('`') || M5Cardputer.Keyboard.isKeyPressed(';')) {
                setMode(PorkchopMode::MENU);
            }
            else if (M5Cardputer.Keyboard.isKeyPressed('s')) {
                if (JanusHog::isConnected()) {
                    JanusHog::requestScan();
                    Display::notify(NoticeKind::STATUS, "C5 SCAN STARTED", 1500, NoticeChannel::TOP_BAR);
                } else {
                    Display::notify(NoticeKind::STATUS, "C5 NOT CONNECTED", 1500, NoticeChannel::TOP_BAR);
                }
            }
            else if (M5Cardputer.Keyboard.isKeyPressed('c')) {
                if (JanusHog::isConnected()) {
                    JanusHog::requestChannelView();
                    Display::notify(NoticeKind::STATUS, "CH VIEW STARTED", 1500, NoticeChannel::TOP_BAR);
                }
            }
            else if (M5Cardputer.Keyboard.isKeyPressed('i')) {
                if (JanusHog::isConnected()) {
                    if (JanusHog::requestImportNewestHandshake()) {
                        Display::notify(NoticeKind::STATUS, "IMPORT STARTED", 1500, NoticeChannel::TOP_BAR);
                    }
                } else {
                    Display::notify(NoticeKind::STATUS, "C5 NOT CONNECTED", 1500, NoticeChannel::TOP_BAR);
                }
            }
            else if (M5Cardputer.Keyboard.isKeyPressed('x')) {
                JanusHog::requestStop();
                Display::notify(NoticeKind::STATUS, "C5 STOP", 1000, NoticeChannel::TOP_BAR);
            }
            break;
        }
        case PorkchopMode::CHARGING:
            ChargingMode::update();
            if (ChargingMode::shouldExit()) {
                setMode(PorkchopMode::IDLE);
            }
            break;
        case PorkchopMode::PIGPASS_MODE:
            PigpassMode::update();
            if (!PigpassMode::isRunning()) {
                setMode(PorkchopMode::MENU);
            }
            break;
        default:
            break;
    }
}

uint32_t Porkchop::getUptime() const {
    return (millis() - startTime) / 1000;
}

uint16_t Porkchop::getHandshakeCount() const {
    // Include both handshakes and PMKIDs - both are crackable captures
    return OinkMode::getCompleteHandshakeCount() + OinkMode::getPMKIDCount();
}

uint16_t Porkchop::getNetworkCount() const {
    return OinkMode::getNetworkCount();
}

uint16_t Porkchop::getDeauthCount() const {
    return OinkMode::getDeauthCount();
}
