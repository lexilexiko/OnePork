// Display management for M5Cardputer
#pragma once

#include <M5Unified.h>

// Forward declarations
enum class PorkchopMode : uint8_t;

// Display layout constants (240x135 screen)
#define DISPLAY_W 240
#define DISPLAY_H 135
#define TOP_BAR_H 14
#define BOTTOM_BAR_H 14
#define MAIN_H (DISPLAY_H - TOP_BAR_H - BOTTOM_BAR_H)

// Fixed farm / Terraria-ish UI palette (color themes removed)
struct UiStyle {
    static constexpr uint16_t BG     = 0x2145;  // deep slate
    static constexpr uint16_t PANEL  = 0x3A8A;  // row panel
    static constexpr uint16_t TITLE  = 0xFFE0;  // gold
    static constexpr uint16_t TEXT   = 0xEF5D;  // warm cream
    static constexpr uint16_t DIM    = 0x9CD3;  // dim text
    static constexpr uint16_t PINK   = 0xFDB6;  // pig pink accent
    static constexpr uint16_t GREEN  = 0x07E0;
    static constexpr uint16_t CYAN   = 0x07FF;
    static constexpr uint16_t RED    = 0xF800;
    static constexpr uint16_t GOLD   = 0xFE60;
    static constexpr uint16_t PURPLE = 0xB01F;
    static constexpr uint16_t DIRT   = 0x6A20;
};

enum class NoticeKind : uint8_t {
    REWARD,
    STATUS,
    WARNING,
    ERROR
};

enum class NoticeChannel : uint8_t {
    AUTO,
    TOAST,
    TOP_BAR
};

// Fixed colorful UI colors (no theme cycling)
uint16_t getColorFG();
uint16_t getColorBG();

// Compatibility macros
#define COLOR_BG getColorBG()
#define COLOR_FG getColorFG()
#define COLOR_ACCENT  (UiStyle::PINK)
#define COLOR_WARNING (UiStyle::GOLD)
#define COLOR_DANGER  (UiStyle::RED)
#define COLOR_SUCCESS (UiStyle::GREEN)

// Shared colorful list helpers for submenus
void uiListBackground(M5Canvas& canvas);
void uiListRow(M5Canvas& canvas, int y, int lineH, bool selected, uint16_t accent = UiStyle::PINK);

class Display {
public:
    static void init();
    static void update();
    static void clear();

    // Upload progress tracking
    static bool uploadInProgress;
    static uint8_t uploadProgress;
    static char uploadStatus[64];
    static uint32_t uploadStartTime;
    static void setUploadProgress(bool inProgress, uint8_t progress, const char* status);
    static void clearUploadProgress();
    static bool shouldShowUploadProgress();
    static void drawUploadProgress(M5Canvas& topBar);
    static void drawUploadProgressDirect();

    // Top bar status messaging (single-line)
    static void setTopBarMessage(const String& message, uint32_t durationMs = 0);
    static void setTopBarMessage(const char* message, uint32_t durationMs = 0);
    static void clearTopBarMessage();
    static void requestTopBarMessage(const char* message, uint32_t durationMs = 0);

    // Canvas access for direct drawing
    static M5Canvas& getTopBar() { return topBar; }
    static M5Canvas& getMain() { return mainCanvas; }
    static M5Canvas& getBottomBar() { return bottomBar; }
    
    // Helper functions
    static void pushAll();
    static void showBootSplash();  // 3-screen boot animation
    static void showInfoBox(const char* title, const char* line1,
                           const char* line2 = "", bool blocking = true);
    static bool showConfirmBox(const char* title, const char* message);
    static void showProgress(const String& title, uint8_t percent);
    static void showProgress(const char* title, uint8_t percent);
    static void showToast(const String& message, uint32_t durationMs = 2000);  // Quick non-blocking message
    static void showToast(const char* message, uint32_t durationMs = 2000);    // Literal-friendly overload
    static void notify(NoticeKind kind, const String& message,
                       uint32_t durationMs = 0,
                       NoticeChannel channel = NoticeChannel::AUTO);
    static void notify(NoticeKind kind, const char* message,
                       uint32_t durationMs = 0,
                       NoticeChannel channel = NoticeChannel::AUTO);
    static void showLevelUp(uint8_t oldLevel, uint8_t newLevel);  // RPG level up popup

    // Mode-specific UI functions
    static void drawPigSyncDeviceSelect(M5Canvas& canvas);  // PigSync device selection UI
    static void showClassPromotion(const char* oldClass, const char* newClass);  // Class tier promotion popup
    static void showChallenges();  // Session challenges overlay (press '1')
    
    // LED effects (NeoPixel on GPIO 21)
    static void flashSiren(uint8_t cycles = 3);  // Red/blue alternating flash
    static void setLED(uint8_t r, uint8_t g, uint8_t b);  // Static LED glow
    
    // PWNED banner (shown in top bar for 1 minute after capture)
    static void showLoot(const char* ssid);
    
    // Bottom bar overlay (for confirmation dialogs)
    static void setBottomOverlay(const char* message);  // Set custom bottom bar text
    static void clearBottomOverlay();                     // Clear overlay, restore normal
    
    // Status indicators
    static void setGPSStatus(bool hasFix);
    static void setWiFiStatus(bool connected);
    static void setMLStatus(bool active);
    
    // Screen dimming
    static void resetDimTimer();      // Call on any user input
    static void updateDimming();      // Call in update loop
    static bool isDimmed() { return dimmed; }
    static void toggleScreenPower(); // Toggle screen on/off

    // Screenshot
    static bool takeScreenshot();     // Save screen to SD card, returns success
    static bool isSnapping() { return snapping; }  // True during screenshot save

    // Screen shake effect (captures, impacts)
    static void triggerScreenShake(uint8_t intensity = 3, uint16_t durationMs = 200);
    static bool isShaking();
    static float getShakeDecay();      // 1.0 at start → 0.0 at end
    static uint8_t getShakeIntensity();

    // Temporarily release the main canvas sprite (~26KB) to free heap for
    // memory-heavy blocking operations (e.g. TLS sync). Caller MUST restore it
    // before the next render. Safe only while the render loop is blocked.
    // mainCanvas is backed by a fixed static buffer (never heap-allocated) so it
    // never fragments the heap. During TLS sync the same buffer is lent to mbedTLS
    // as a scratch arena (see TlsArena) — these accessors expose it for that.
    static uint8_t* mainCanvasBuffer();
    static size_t   mainCanvasBufferSize();

private:
    // Screen shake state
    static bool screenShakeActive;
    static uint32_t screenShakeStart;
    static uint16_t screenShakeDuration;
    static uint8_t screenShakeIntensity;
    static M5Canvas topBar;
    static M5Canvas mainCanvas;
    static M5Canvas bottomBar;
    
    static bool gpsStatus;
    static bool wifiStatus;
    static bool mlStatus;
    
    // Dimming state
    static uint32_t lastActivityTime;
    static bool dimmed;
    static bool screenForcedOff;
    
    // Screenshot state
    static bool snapping;

    // Toast state
    static char toastMessage[160];
    static uint32_t toastStartTime;
    static uint32_t toastDurationMs;
    static bool toastActive;
    static char topBarMessage[96];
    static uint32_t topBarMessageStart;
    static uint32_t topBarMessageDuration;
    static bool topBarMessageTwoLineActive;

    // Bottom bar overlay
    static char bottomOverlay[96];
    static volatile bool pendingTopBarMessage;
    static char pendingTopBarMessageBuf[96];
    static uint32_t pendingTopBarDurationMs;
    
    static void drawTopBar();
    static void drawBottomBar();
    static void drawTopBarMessageTwoLineDirect(int offsetX = 0, int offsetY = 0);
    static void drawModeInfo(M5Canvas& canvas, PorkchopMode mode);
    static void drawSettingsScreen(M5Canvas& canvas);
    static void drawAboutScreen(M5Canvas& canvas);
    static void drawFileTransferScreen(M5Canvas& canvas);
    
public:
    // About screen easter egg handlers (called from porkchop.cpp)
    static void onAboutEnterPressed();
    static void resetAboutState();
};
