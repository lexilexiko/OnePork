// IR PORK — power blast (builtin NA/EU packs) + custom SD files

#include "ir_pork.h"
#include "ir_power/ir_power_tx.h"
#include "../ui/display.h"
#include "../audio/sfx.h"
#include "../piglet/avatar.h"
#include "../core/config.h"
#include <M5Cardputer.h>
#include <M5Unified.h>
#include <SD.h>
#include <string.h>
#include <stdlib.h>

bool IrPorkMode::running = false;
IrPorkMode::Phase IrPorkMode::phase = IrPorkMode::Phase::READY;
IrPorkMode::Pack IrPorkMode::pack = IrPorkMode::Pack::BUILTIN;
IrPorkMode::Code IrPorkMode::codes[MAX_CODES] = {};
uint8_t IrPorkMode::codeCount = 0;
uint8_t IrPorkMode::blastIndex = 0;
uint8_t IrPorkMode::blastTotal = 0;
uint32_t IrPorkMode::nextSendMs = 0;
char IrPorkMode::packName[28] = "P0W3R N4";
char IrPorkMode::statusMsg[40] = "";
char IrPorkMode::fileNames[MAX_FILES][28] = {{0}};
uint8_t IrPorkMode::fileCount = 0;
uint8_t IrPorkMode::fileSel = 0;
uint8_t IrPorkMode::fileScroll = 0;

void IrPorkMode::muteAudioForIr() {
    // IR bitbang + speaker = glitchy piezo / stacked tones
    SFX::setMuted(true);
    SFX::stop();
    M5.Speaker.stop();
}

void IrPorkMode::loadBuiltinPack() {
    pack = Pack::BUILTIN;
    codeCount = 0;
    blastTotal = IrPower::getCodeCount();
    const char* reg = (IrPower::getRegion() == IR_REGION_EU) ? "3U" : "N4";
    snprintf(packName, sizeof(packName), "P0W3R %s", reg);
}

bool IrPorkMode::loadFile(const char* path) {
    if (!Config::isSDAvailable()) return false;
    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    codeCount = 0;
    pack = Pack::CUSTOM;
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    strncpy(packName, base, sizeof(packName) - 1);
    packName[sizeof(packName) - 1] = '\0';

    char line[96];
    while (f.available() && codeCount < MAX_CODES) {
        size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = '\0';
        char* cr = strchr(line, '\r');
        if (cr) *cr = '\0';
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#' || *p == ';') continue;

        char proto[16] = {0}, aStr[16] = {0}, cStr[16] = {0}, bStr[16] = {0}, name[24] = {0};
        int got = sscanf(p, "%15s %15s %15s %15s %23s", proto, aStr, cStr, bStr, name);
        if (got < 2) continue;

        Proto pr = Proto::NEC;
        if (strcasecmp(proto, "NEC") == 0) pr = Proto::NEC;
        else if (strcasecmp(proto, "SAMSUNG") == 0 || strcasecmp(proto, "SAM") == 0) pr = Proto::SAMSUNG;
        else if (strcasecmp(proto, "SONY") == 0) pr = Proto::SONY;
        else continue;

        uint16_t addr = (uint16_t)strtoul(aStr, nullptr, 0);
        uint16_t cmd = 0;
        uint8_t bits = 0;

        if (pr == Proto::SONY) {
            cmd = addr;
            addr = 0;
            if (got >= 3 && cStr[0] >= '0' && cStr[0] <= '9') {
                bits = (uint8_t)strtoul(cStr, nullptr, 0);
                if (got >= 4 && bStr[0] && !((bStr[0] >= '0' && bStr[0] <= '9') || bStr[0] == '0'))
                    strncpy(name, bStr, sizeof(name) - 1);
            } else {
                if (got >= 3) strncpy(name, cStr, sizeof(name) - 1);
                bits = 12;
            }
        } else {
            if (got >= 3) cmd = (uint16_t)strtoul(cStr, nullptr, 0);
            if (got >= 4 && bStr[0]) {
                bool num = (bStr[0] >= '0' && bStr[0] <= '9') ||
                           (bStr[0] == '0' && (bStr[1] == 'x' || bStr[1] == 'X'));
                if (!num) strncpy(name, bStr, sizeof(name) - 1);
            }
        }

        Code& x = codes[codeCount++];
        x.proto = pr;
        x.addr = addr;
        x.cmd = cmd;
        x.bits = bits;
        if (name[0]) {
            strncpy(x.name, name, sizeof(x.name) - 1);
            x.name[sizeof(x.name) - 1] = '\0';
        } else {
            snprintf(x.name, sizeof(x.name), "%s#%u", proto, (unsigned)codeCount);
        }
    }
    f.close();
    blastTotal = codeCount;
    return codeCount > 0;
}

void IrPorkMode::scanIrFiles() {
    fileCount = 0;
    fileSel = 0;
    fileScroll = 0;
    if (!Config::isSDAvailable()) return;

    auto addFile = [](const char* base) {
        if (fileCount >= MAX_FILES) return;
        for (uint8_t i = 0; i < fileCount; i++)
            if (strcmp(fileNames[i], base) == 0) return;
        strncpy(fileNames[fileCount], base, sizeof(fileNames[0]) - 1);
        fileNames[fileCount][sizeof(fileNames[0]) - 1] = '\0';
        fileCount++;
    };

    auto scanDir = [&](const char* dir, bool anyTxt) {
        File d = SD.open(dir);
        if (!d || !d.isDirectory()) { if (d) d.close(); return; }
        File e = d.openNextFile();
        while (e && fileCount < MAX_FILES) {
            if (!e.isDirectory()) {
                const char* nm = e.name();
                const char* base = strrchr(nm, '/');
                base = base ? base + 1 : nm;
                size_t len = strlen(base);
                if (len > 3) {
                    const char* ext3 = base + len - 3;
                    const char* ext4 = (len > 4) ? base + len - 4 : "";
                    if (strcasecmp(ext3, ".ir") == 0 ||
                        (anyTxt && strcasecmp(ext4, ".txt") == 0))
                        addFile(base);
                }
            }
            e.close();
            e = d.openNextFile();
        }
        d.close();
    };

    scanDir("/ir", true);
    scanDir("/IR", true);
    scanDir("/", false);
}

void IrPorkMode::init() {
    running = false;
    phase = Phase::READY;
}

void IrPorkMode::start() {
    running = true;
    phase = Phase::READY;
    blastIndex = 0;
    IrPower::setRegion(IR_REGION_NA);
    loadBuiltinPack();
    snprintf(statusMsg, sizeof(statusMsg), "SPC=F1R3  E=F1L3  R=N4/3U");
    SFX::setMuted(false);
    SFX::stop();  // clear any stacked ambient from previous mode
    SFX::play(SFX::MODE_ENTER);
    Display::notify(NoticeKind::STATUS, "1RP0RK - P01NT 4T TV", 2200, NoticeChannel::TOP_BAR);
    Avatar::setState(AvatarState::HUNTING);
    pinMode(IrPower::IR_TX_PIN, OUTPUT);
    digitalWrite(IrPower::IR_TX_PIN, HIGH);  // off (active-low)
}

void IrPorkMode::stop() {
    running = false;
    phase = Phase::READY;
    digitalWrite(IrPower::IR_TX_PIN, HIGH);
    SFX::setMuted(false);  // restore audio after IR session
    SFX::stop();
    Avatar::setSparkleStorm(false);
    Avatar::setState(AvatarState::NEUTRAL);
    Avatar::waveRipple(WaveMode::NONE);  // ensure no leftover rings
}

void IrPorkMode::onHotkeyAgain() {
    if (!running || phase == Phase::BLAST) return;
    phase = Phase::FILE_PICK;
    scanIrFiles();
    if (fileCount == 0) {
        snprintf(statusMsg, sizeof(statusMsg), "NO /ir/*.txt ON SD");
        Display::notify(NoticeKind::WARNING, "PUT FILES IN /ir/", 2500, NoticeChannel::TOP_BAR);
    } else {
        snprintf(statusMsg, sizeof(statusMsg), "PICK FILE  ENT=LOAD");
    }
    SFX::play(SFX::CLICK);
}

void IrPorkMode::startBlast() {
    if (pack == Pack::BUILTIN) {
        blastTotal = IrPower::getCodeCount();
    } else {
        blastTotal = codeCount;
    }
    if (blastTotal == 0) {
        snprintf(statusMsg, sizeof(statusMsg), "NO CODES");
        return;
    }
    // One laser-charge SFX before mute — IR bitbang hates the speaker
    SFX::setMuted(false);
    SFX::stop();
    SFX::play(SFX::IR_FIRE);

    phase = Phase::BLAST;
    blastIndex = 0;
    // Let IR_FIRE finish (~115ms) before TX + mute (smoother, less glitch)
    nextSendMs = millis() + 280;
    snprintf(statusMsg, sizeof(statusMsg), "FIRING...");
    Avatar::setState(AvatarState::EXCITED);
    Avatar::setSparkleStorm(true);   // whole pig covered in stars
    Avatar::setGrassMoving(false);   // free CPU during IR TX
    Avatar::waveRipple(WaveMode::NONE);  // no radio-ring anim in IR
    Avatar::setManualWalk(false);
}

void IrPorkMode::handleInput() {
    static bool keyWas = false;
    bool any = M5Cardputer.Keyboard.isPressed();
    if (!any) { keyWas = false; return; }
    if (keyWas) return;
    keyWas = true;

    if (M5Cardputer.Keyboard.isKeyPressed('`') ||
        M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
        if (phase == Phase::FILE_PICK) {
            phase = Phase::READY;
            snprintf(statusMsg, sizeof(statusMsg), "SPC=F1R3  E=F1L3  R=N4/3U");
            return;
        }
        if (phase == Phase::BLAST) {
            phase = Phase::DONE;
            snprintf(statusMsg, sizeof(statusMsg), "STOP %u/%u",
                     (unsigned)blastIndex, (unsigned)blastTotal);
            Avatar::setSparkleStorm(false);
            Avatar::waveRipple(WaveMode::NONE);
            SFX::setMuted(false);
            return;
        }
        stop();
        return;
    }

    if (phase == Phase::FILE_PICK) {
        if (M5Cardputer.Keyboard.isKeyPressed(';') ||
            M5Cardputer.Keyboard.isKeyPressed(',')) {
            if (fileSel > 0) fileSel--;
            if (fileSel < fileScroll) fileScroll = fileSel;
            SFX::play(SFX::MENU_CLICK);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('.') ||
            M5Cardputer.Keyboard.isKeyPressed('/')) {
            if (fileCount && fileSel + 1 < fileCount) fileSel++;
            if (fileSel >= fileScroll + 5) fileScroll = (uint8_t)(fileSel - 4);
            SFX::play(SFX::MENU_CLICK);
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER) ||
            M5Cardputer.Keyboard.isKeyPressed(' ')) {
            if (fileCount == 0) return;
            char path[72];
            snprintf(path, sizeof(path), "/ir/%s", fileNames[fileSel]);
            if (!SD.exists(path)) snprintf(path, sizeof(path), "/IR/%s", fileNames[fileSel]);
            if (!SD.exists(path)) snprintf(path, sizeof(path), "/%s", fileNames[fileSel]);
            if (loadFile(path)) {
                phase = Phase::READY;
                snprintf(statusMsg, sizeof(statusMsg), "LOADED %u CODES", (unsigned)codeCount);
                Display::notify(NoticeKind::STATUS, packName, 2000, NoticeChannel::TOP_BAR);
                SFX::play(SFX::CONFIRM);
            } else {
                snprintf(statusMsg, sizeof(statusMsg), "LOAD FAIL");
                SFX::play(SFX::ERROR);
            }
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('b') ||
            M5Cardputer.Keyboard.isKeyPressed('B')) {
            loadBuiltinPack();
            phase = Phase::READY;
            snprintf(statusMsg, sizeof(statusMsg), "P0W3R P4CK");
            SFX::play(SFX::CLICK);
            return;
        }
        return;
    }

    if (phase == Phase::BLAST) {
        if (M5Cardputer.Keyboard.isKeyPressed('x') ||
            M5Cardputer.Keyboard.isKeyPressed('X') ||
            M5Cardputer.Keyboard.isKeyPressed('.')) {
            phase = Phase::DONE;
            snprintf(statusMsg, sizeof(statusMsg), "STOP %u/%u",
                     (unsigned)blastIndex, (unsigned)blastTotal);
            Avatar::setSparkleStorm(false);
            Avatar::waveRipple(WaveMode::NONE);
            SFX::setMuted(false);
        }
        return;
    }

    if (M5Cardputer.Keyboard.isKeyPressed(' ') ||
        M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
        startBlast();
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('e') ||
        M5Cardputer.Keyboard.isKeyPressed('E') ||
        M5Cardputer.Keyboard.isKeyPressed('i') ||
        M5Cardputer.Keyboard.isKeyPressed('I')) {
        onHotkeyAgain();
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('r') ||
        M5Cardputer.Keyboard.isKeyPressed('R')) {
        uint8_t r = IrPower::getRegion();
        IrPower::setRegion(r == IR_REGION_NA ? IR_REGION_EU : IR_REGION_NA);
        if (pack == Pack::BUILTIN) loadBuiltinPack();
        snprintf(statusMsg, sizeof(statusMsg), "REGION %s  N=%u",
                 IrPower::getRegion() == IR_REGION_EU ? "3U" : "N4",
                 (unsigned)IrPower::getCodeCount());
        SFX::play(SFX::CLICK);
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('b') ||
        M5Cardputer.Keyboard.isKeyPressed('B')) {
        loadBuiltinPack();
        phase = Phase::READY;
        snprintf(statusMsg, sizeof(statusMsg), "P0W3R P4CK");
        SFX::play(SFX::CLICK);
        return;
    }
}

void IrPorkMode::update() {
    if (!running) return;
    handleInput();
    if (!running) return;

    if (phase != Phase::BLAST) return;

    uint32_t now = millis();
    if (now < nextSendMs) return;

    if (blastIndex >= blastTotal) {
        phase = Phase::DONE;
        snprintf(statusMsg, sizeof(statusMsg), "DONE %u CODES", (unsigned)blastTotal);
        Avatar::waveRipple(WaveMode::NONE);
        Avatar::setSparkleStorm(false);
        Avatar::setState(AvatarState::HAPPY);
        Avatar::triggerSparkles(10);  // final star burst
        // Safe to beep again after IR TX finished
        SFX::setMuted(false);
        SFX::stop();
        SFX::play(SFX::CONFIRM);
        return;
    }

    // Keep speaker fully muted every shot (bitbang + SFX = foul noise)
    muteAudioForIr();

    if (pack == Pack::BUILTIN) {
        IrPower::sendCode(blastIndex);
        // Slightly longer gap → less frame-skip feel + speaker settle
        nextSendMs = millis() + 28;
    } else {
        const Code& c = codes[blastIndex];
        switch (c.proto) {
            case Proto::NEC:     IrPower::sendNEC(c.addr, (uint8_t)c.cmd, 1); break;
            case Proto::SAMSUNG: IrPower::sendSamsung(c.addr, c.cmd, 1); break;
            case Proto::SONY:    IrPower::sendSony(c.cmd ? c.cmd : c.addr,
                                                  c.bits ? c.bits : 12, 2); break;
        }
        nextSendMs = millis() + 100;
    }

    // Stars only — no circular wave/signal rings (looks wrong in IR)
    if ((blastIndex % 12) == 0)
        Avatar::setState(AvatarState::EXCITED);

    blastIndex++;
}

void IrPorkMode::getStatusLine(char* buf, size_t n) {
    if (!buf || n == 0) return;
    if (phase == Phase::FILE_PICK) {
        snprintf(buf, n, "IR FILE %u/%u  ;/. ENT",
                 fileCount ? (unsigned)(fileSel + 1) : 0, (unsigned)fileCount);
        return;
    }
    if (phase == Phase::BLAST) {
        snprintf(buf, n, "F1R3 %u/%u  X=ST0P",
                 (unsigned)blastIndex, (unsigned)blastTotal);
        return;
    }
    if (pack == Pack::BUILTIN) {
        snprintf(buf, n, "P0W3R %s N:%u  SPC E R",
                 IrPower::getRegion() == IR_REGION_EU ? "3U" : "N4",
                 (unsigned)IrPower::getCodeCount());
    } else {
        snprintf(buf, n, "CUST0M N:%u  SPC E B=P0W3R", (unsigned)codeCount);
    }
}

void IrPorkMode::getTopBarLabel(char* buf, size_t n) {
    if (!buf || n == 0) return;
    if (phase == Phase::BLAST)
        snprintf(buf, n, "1R %u/%u", (unsigned)blastIndex, (unsigned)blastTotal);
    else if (phase == Phase::FILE_PICK)
        snprintf(buf, n, "1R F1L3S");
    else if (pack == Pack::BUILTIN)
        snprintf(buf, n, "1R %s", IrPower::getRegion() == IR_REGION_EU ? "3U" : "N4");
    else
        snprintf(buf, n, "1RP0RK");
}

void IrPorkMode::draw(M5Canvas& canvas) {
    // Full scene — pig visible
    Avatar::draw(canvas);
    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);

    // === BLAST: no big menu — only pig + thin fire HUD ===
    if (phase == Phase::BLAST) {
        // Slim top progress so the pig stays the star
        int barW = 180;
        int fill = blastTotal ? (barW * (int)blastIndex / (int)blastTotal) : 0;
        canvas.fillRoundRect(28, 2, barW + 4, 12, 2, 0x1082);
        canvas.drawRoundRect(28, 2, barW + 4, 12, 2, 0xF800);
        if (fill > 0) canvas.fillRect(30, 4, fill, 8, 0xF800);
        canvas.setTextColor(0xFFFF);
        char t[24];
        snprintf(t, sizeof(t), "%u/%u", (unsigned)blastIndex, (unsigned)blastTotal);
        canvas.setTextDatum(top_center);
        canvas.drawString(t, 120, 3);
        canvas.setTextDatum(top_left);
        return;
    }

    // READY / DONE / FILE — compact panel (not full-screen menu)
    if (phase == Phase::FILE_PICK) {
        canvas.fillRoundRect(4, 14, 232, 88, 3, 0x1082);
        canvas.drawRoundRect(4, 14, 232, 88, 3, 0x07FF);
        canvas.setTextColor(0x07FF);
        canvas.drawString("1RP0RK", 10, 18);
        canvas.setTextColor(0xFFFF);
        canvas.drawString("S3L3CT F1L3  (SD /ir)", 10, 32);
        if (fileCount == 0) {
            canvas.setTextColor(0xF800);
            canvas.drawString("No .txt/.ir in /ir", 10, 50);
            canvas.setTextColor(0xAD55);
            canvas.drawString("B = builtin power pack", 10, 64);
        } else {
            for (uint8_t i = 0; i < 5; i++) {
                uint8_t idx = fileScroll + i;
                if (idx >= fileCount) break;
                int y = 34 + (int)i * 12;
                bool sel = (idx == fileSel);
                if (sel) {
                    canvas.fillRect(8, y - 1, 224, 11, 0x2104);
                    canvas.setTextColor(0xFFE0);
                    canvas.drawString(">", 10, y);
                } else canvas.setTextColor(0xC618);
                canvas.drawString(fileNames[idx], 20, y);
            }
        }
        return;
    }

    // READY / DONE — small card bottom-left, pig clear on the right
    canvas.fillRoundRect(4, 72, 160, 32, 3, 0x1082);
    canvas.drawRoundRect(4, 72, 160, 32, 3, 0x07FF);
    canvas.setTextColor(0x07FF);
    canvas.drawString("1RP0RK", 8, 74);
    canvas.setTextColor(0xFFE0);
    canvas.drawString(packName, 60, 74);

    if (phase == Phase::DONE) {
        canvas.setTextColor(0x07E0);
        canvas.drawString("D0N3 - SPC 4G41N", 8, 88);
    } else {
        canvas.setTextColor(0xAD55);
        char line[40];
        if (pack == Pack::BUILTIN) {
            snprintf(line, sizeof(line), "N:%u  SPC F1R3  R R3G",
                     (unsigned)IrPower::getCodeCount());
        } else {
            snprintf(line, sizeof(line), "N:%u  SPC F1R3  E F1L3",
                     (unsigned)codeCount);
        }
        canvas.drawString(line, 8, 88);
    }
}
