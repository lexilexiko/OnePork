// MicPork — Cardputer Adv/v1 mic dance
//
// Why it failed before:
// 1) Mic.record() queues async — must analyze PREVIOUS buffer (M5 example pattern)
// 2) ADV shares ES8311 with speaker — must fully end Speaker before Mic.begin
// 3) SFX::tone() re-opens speaker mid-mode and kills mic I2S
// 4) Thresholds too high for quiet room / ADV gain

#include "micpork.h"
#include <M5Cardputer.h>
#include <math.h>
#include <string.h>
#include "../ui/display.h"
#include "../piglet/avatar.h"
#include "../piglet/weather.h"
#include "../piglet/seasonal_fx.h"
#include "../piglet/wolf.h"
#include "../core/config.h"
#include "../core/sdlog.h"
#include "../audio/sfx.h"

bool MicPorkMode::running = false;
int16_t MicPorkMode::samples[MicPorkMode::BUF_COUNT][MicPorkMode::SAMPLE_LEN] = {{0}};
uint8_t MicPorkMode::writeBuf = 0;
uint8_t MicPorkMode::readBuf = 1;
bool MicPorkMode::haveAudio = false;
float MicPorkMode::overallLevel = 0.0f;
float MicPorkMode::noiseFloor = 80.0f;  // RMS units, climbs down to real floor
float MicPorkMode::peakHold = 0.0f;
uint32_t MicPorkMode::lastWaveMs = 0;
uint32_t MicPorkMode::lastLogMs = 0;
uint32_t MicPorkMode::sessionStart = 0;
uint8_t MicPorkMode::savedSoundLevel = 1;
bool MicPorkMode::micOk = false;
uint32_t MicPorkMode::recordOkCount = 0;
uint32_t MicPorkMode::recordFailCount = 0;

void MicPorkMode::init() {
    running = false;
    overallLevel = 0.0f;
    noiseFloor = 80.0f;
    peakHold = 0.0f;
    haveAudio = false;
    writeBuf = 0;
    readBuf = 1;
    micOk = false;
    recordOkCount = 0;
    recordFailCount = 0;
    memset(samples, 0, sizeof(samples));
    Avatar::setMicDance(0.0f);
}

bool MicPorkMode::startMicHardware() {
    // Official pattern: Speaker.end() then Mic.begin()
    if (M5.Speaker.isPlaying()) {
        M5.Speaker.stop();
        delay(5);
    }
    // Always end speaker — ADV ES8311 cannot RX while TX path is hot
    M5.Speaker.end();
    delay(20);

    // Boost mic gain a bit for room speech (ADV ES8311 digital path)
    auto mcfg = M5.Mic.config();
    if (mcfg.pin_data_in < 0) {
        Serial.println("[MICPORK] FAIL: pin_data_in < 0 (board mic not configured)");
        return false;
    }
    // ADV: data=46 ws=43 bck=41 — leave pins alone, only gain knobs
    if (mcfg.magnification < 32) mcfg.magnification = 32;
    mcfg.noise_filter_level = 0;  // no filtering — we want raw energy
    mcfg.sample_rate = SAMPLE_RATE;
    mcfg.over_sampling = 2;
    M5.Mic.config(mcfg);

    Serial.printf("[MICPORK] Mic cfg: pin_in=%d ws=%d bck=%d mag=%u rate=%u enabled=%d\n",
                  mcfg.pin_data_in, mcfg.pin_ws, mcfg.pin_bck,
                  (unsigned)mcfg.magnification, (unsigned)mcfg.sample_rate,
                  (int)M5.Mic.isEnabled());

    if (!M5.Mic.begin()) {
        Serial.println("[MICPORK] FAIL: Mic.begin() returned false");
        return false;
    }
    // Warm-up: queue a couple of empty captures so DMA starts
    for (int i = 0; i < 3; i++) {
        M5.Mic.record(samples[0], SAMPLE_LEN, SAMPLE_RATE);
        delay(30);
    }
    Serial.printf("[MICPORK] Mic running=%d recording=%u\n",
                  (int)M5.Mic.isRunning(), (unsigned)M5.Mic.isRecording());
    return M5.Mic.isRunning() && M5.Mic.isEnabled();
}

void MicPorkMode::start() {
    Serial.println("[MICPORK] Starting...");
    init();
    running = true;
    sessionStart = millis();

    // Mute SFX so geiger/waves never re-open speaker mid-mic
    savedSoundLevel = Config::personality().soundLevel;
    Config::personality().soundLevel = 0;

    micOk = startMicHardware();
    if (!micOk) {
        Display::notify(NoticeKind::WARNING, "MIC FAIL - CHECK ADV", 3000, NoticeChannel::TOP_BAR);
        SDLog::log("PORK", "MICPORK mic begin failed");
    } else {
        Display::notify(NoticeKind::STATUS, "MICPORK - SPEAK!", 2500, NoticeChannel::TOP_BAR);
        SDLog::log("PORK", "Mode: MICPORK ok pin=%d", M5.Mic.config().pin_data_in);
    }

    Avatar::setState(AvatarState::EXCITED);
    Avatar::setMicDance(0.0f);
}

void MicPorkMode::stop() {
    if (!running) return;
    Serial.println("[MICPORK] Stopping...");
    running = false;
    Avatar::setMicDance(0.0f);
    Avatar::waveRipple(WaveMode::NONE);
    Avatar::setState(AvatarState::NEUTRAL);

    // Drain record queue
    uint32_t t0 = millis();
    while (M5.Mic.isRecording() && (millis() - t0) < 200) {
        delay(1);
    }
    M5.Mic.end();
    delay(15);

    // Restore speaker for SFX
    if (!M5.Speaker.isRunning()) {
        M5.Speaker.begin();
    }
    Config::personality().soundLevel = savedSoundLevel;
    Serial.printf("[MICPORK] Stopped ok=%lu fail=%lu\n",
                  (unsigned long)recordOkCount, (unsigned long)recordFailCount);
}

uint8_t MicPorkMode::getLevelPct() {
    int v = (int)(overallLevel * 100.0f + 0.5f);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    return (uint8_t)v;
}

void MicPorkMode::analyzeBuffer(const int16_t* data) {
    // Peak + RMS
    int32_t peak = 0;
    double acc = 0.0;
    for (size_t i = 0; i < SAMPLE_LEN; i++) {
        int32_t s = data[i];
        if (s < 0) s = -s;
        if (s > peak) peak = s;
        acc += (double)data[i] * (double)data[i];
    }
    float rms = (float)sqrt(acc / (double)SAMPLE_LEN);

    // Auto noise floor (slow rise, faster fall toward quiet)
    if (rms < noiseFloor) {
        noiseFloor = noiseFloor * 0.90f + rms * 0.10f;
    } else {
        noiseFloor = noiseFloor * 0.995f + rms * 0.005f;
    }
    if (noiseFloor < 20.0f) noiseFloor = 20.0f;

    // Level above floor — sensitive for speech
    float span = rms - noiseFloor * 1.4f;
    if (span < 0.0f) span = 0.0f;
    // Map: ~400-1500 RMS above floor = full dance (ADV levels vary)
    float level = span / 900.0f;
    // Peak helps for short claps
    float peakL = ((float)peak - noiseFloor * 2.0f) / 8000.0f;
    if (peakL > level) level = peakL;
    if (level > 1.0f) level = 1.0f;

    // Responsive smoothing
    overallLevel = overallLevel * 0.35f + level * 0.65f;
    if (overallLevel > peakHold) peakHold = overallLevel;
    else peakHold *= 0.92f;

    if ((millis() - lastLogMs) > 1000) {
        lastLogMs = millis();
        Serial.printf("[MICPORK] rms=%.0f floor=%.0f peak=%ld lvl=%.2f dance=%.2f ok=%lu\n",
                      rms, noiseFloor, (long)peak, level, overallLevel,
                      (unsigned long)recordOkCount);
    }
}

void MicPorkMode::handleInput() {
    // Edge-detect M to exit (keys.word is flaky on ADV)
    static bool mWas = false;
    bool mNow = M5Cardputer.Keyboard.isKeyPressed('m') ||
                M5Cardputer.Keyboard.isKeyPressed('M');
    if (mNow && !mWas) {
        mWas = true;
        stop();
        return;
    }
    if (!mNow) mWas = false;
}

void MicPorkMode::update() {
    if (!running) return;
    handleInput();
    if (!running) return;

    if (!micOk) {
        // Retry mic every 2s
        static uint32_t lastRetry = 0;
        if (millis() - lastRetry > 2000) {
            lastRetry = millis();
            micOk = startMicHardware();
        }
        Avatar::setMicDance(0.0f);
        return;
    }

    // Double-buffer (same as M5Cardputer mic.ino):
    // record() queues writeBuf async; previous buffer is complete for analysis.
    if (M5.Mic.isEnabled()) {
        if (M5.Mic.record(samples[writeBuf], SAMPLE_LEN, SAMPLE_RATE)) {
            recordOkCount++;
            if (haveAudio) {
                analyzeBuffer(samples[readBuf]);
            } else {
                // First successful queue — next swap will have data
                haveAudio = true;
            }
            // Swap roles
            readBuf = writeBuf;
            writeBuf = (uint8_t)((writeBuf + 1) % BUF_COUNT);
        } else {
            recordFailCount++;
        }
    } else {
        recordFailCount++;
        micOk = false;
    }

    // Pig IS the spectrometer
    Avatar::setMicDance(overallLevel);

    if (overallLevel > 0.45f) {
        Avatar::setState(AvatarState::EXCITED);
    } else if (overallLevel > 0.12f) {
        Avatar::setState(AvatarState::HAPPY);
    } else {
        Avatar::setState(AvatarState::NEUTRAL);
    }

    // Jump on strong peaks
    static uint32_t lastJump = 0;
    uint32_t now = millis();
    if (overallLevel > 0.50f && (now - lastJump) > 280) {
        Avatar::cuteJump();
        lastJump = now;
    }

    // Rings only when actually hearing (not noise floor)
    if (overallLevel > 0.08f) {
        uint8_t intensity = 2 + (uint8_t)(overallLevel * 3.5f);
        if (intensity > 5) intensity = 5;
        if ((now - lastWaveMs) > 100) {
            Avatar::waveRipple(WaveMode::OUTGOING, intensity);
            lastWaveMs = now;
        }
    }
}

void MicPorkMode::draw(M5Canvas& canvas) {
    // Scene + pig only (no spectrum bar UI — pig is the meter)
    // Clouds drawn inside Avatar (behind tree); rain/birds on top
    Avatar::draw(canvas);
    Wolf::draw(canvas);
    Weather::drawBirds(canvas, COLOR_FG);
    Weather::draw(canvas, COLOR_FG, COLOR_BG);
    SeasonalFx::draw(canvas);

    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);
    if (!micOk) {
        canvas.setTextColor(UiStyle::RED);
        canvas.drawString("MIC ERR", 4, 2);
    } else {
        canvas.setTextColor(overallLevel > 0.08f ? UiStyle::GREEN : UiStyle::RED);
        canvas.drawString(overallLevel > 0.08f ? "HEAR" : "MIC", 4, 2);
        // Tiny level tick (debug aid, not a spectrum UI)
        int lw = (int)(overallLevel * 36.0f);
        canvas.fillRect(30, 3, 40, 6, UiStyle::PANEL);
        if (lw > 0) canvas.fillRect(31, 4, lw, 4, UiStyle::GREEN);
    }
    canvas.setTextColor(UiStyle::DIM);
    canvas.drawString("M=STOP", DISPLAY_W - 48, 2);
}
