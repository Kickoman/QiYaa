// Sprite coordinates inside Winamp 2.x skin bitmaps and element positions in the
// main window. Values taken from webamp (skinSprites.ts, css/main-window.css),
// MIT, (c) Jordan Eldredge — see THIRD_PARTY.md.
#pragma once

#include <QPoint>
#include <QRect>

namespace qiyaa::sprites {

// ------------------------------------------------------------------ MAIN.BMP
inline constexpr QRect kMainBackground{0, 0, 275, 116};

// ------------------------------------------------------------------ TITLEBAR.BMP
inline constexpr QRect kTitleBar{27, 15, 275, 14};
inline constexpr QRect kTitleBarSelected{27, 0, 275, 14};
inline constexpr QRect kOptionsButton{0, 0, 9, 9};
inline constexpr QRect kOptionsButtonDown{0, 9, 9, 9};
inline constexpr QRect kMinimizeButton{9, 0, 9, 9};
inline constexpr QRect kMinimizeButtonDown{9, 9, 9, 9};
inline constexpr QRect kShadeButton{0, 18, 9, 9};
inline constexpr QRect kShadeButtonDown{9, 18, 9, 9};
inline constexpr QRect kCloseButton{18, 0, 9, 9};
inline constexpr QRect kCloseButtonDown{18, 9, 9, 9};
inline constexpr QRect kClutterBar{304, 0, 8, 43};

// ------------------------------------------------------------------ CBUTTONS.BMP
struct ButtonSprite {
    QRect normal;
    QRect pressed;
};
inline constexpr ButtonSprite kPrevious{{0, 0, 23, 18}, {0, 18, 23, 18}};
inline constexpr ButtonSprite kPlay{{23, 0, 23, 18}, {23, 18, 23, 18}};
inline constexpr ButtonSprite kPause{{46, 0, 23, 18}, {46, 18, 23, 18}};
inline constexpr ButtonSprite kStop{{69, 0, 23, 18}, {69, 18, 23, 18}};
inline constexpr ButtonSprite kNext{{92, 0, 22, 18}, {92, 18, 22, 18}};
inline constexpr ButtonSprite kEject{{114, 0, 22, 16}, {114, 16, 22, 16}};

// ------------------------------------------------------------------ PLAYPAUS.BMP
inline constexpr QRect kPlayingIndicator{0, 0, 9, 9};
inline constexpr QRect kPausedIndicator{9, 0, 9, 9};
inline constexpr QRect kStoppedIndicator{18, 0, 9, 9};
inline constexpr QRect kWorkingIndicator{39, 0, 9, 9};

// ------------------------------------------------------------------ MONOSTER.BMP
inline constexpr QRect kStereo{0, 12, 29, 12};
inline constexpr QRect kStereoSelected{0, 0, 29, 12};
inline constexpr QRect kMono{29, 12, 27, 12};
inline constexpr QRect kMonoSelected{29, 0, 27, 12};

// ------------------------------------------------------------------ NUMBERS.BMP / NUMS_EX.BMP
inline constexpr int kDigitW = 9;
inline constexpr int kDigitH = 13;
inline constexpr QRect digit(int d) { return {d * kDigitW, 0, kDigitW, kDigitH}; }
inline constexpr QRect kMinusSign{20, 6, 5, 1};        // numbers.bmp
inline constexpr QRect kMinusSignEx{99, 0, 9, 13};     // nums_ex.bmp

// ------------------------------------------------------------------ POSBAR.BMP
inline constexpr QRect kPositionBackground{0, 0, 248, 10};
inline constexpr QRect kPositionThumb{248, 0, 29, 10};
inline constexpr QRect kPositionThumbSelected{278, 0, 29, 10};

// ------------------------------------------------------------------ SHUFREP.BMP
struct ToggleSprite {
    QRect off;
    QRect offPressed;
    QRect on;
    QRect onPressed;
};
inline constexpr ToggleSprite kShuffle{{28, 0, 47, 15}, {28, 15, 47, 15}, {28, 30, 47, 15}, {28, 45, 47, 15}};
inline constexpr ToggleSprite kRepeat{{0, 0, 28, 15}, {0, 15, 28, 15}, {0, 30, 28, 15}, {0, 45, 28, 15}};
inline constexpr ToggleSprite kEqButton{{0, 61, 23, 12}, {46, 61, 23, 12}, {0, 73, 23, 12}, {46, 73, 23, 12}};
inline constexpr ToggleSprite kPlButton{{23, 61, 23, 12}, {69, 61, 23, 12}, {23, 73, 23, 12}, {69, 73, 23, 12}};

// ------------------------------------------------------------------ VOLUME.BMP / BALANCE.BMP
// Background is a vertical strip of 28 frames, 15px apart, 13px high.
inline constexpr int kSliderFrameStep = 15;
inline constexpr int kSliderFrameH = 13;
inline constexpr QRect kVolumeThumb{15, 422, 14, 11};
inline constexpr QRect kVolumeThumbSelected{0, 422, 14, 11};
inline constexpr QRect kBalanceThumb{15, 422, 14, 11};
inline constexpr QRect kBalanceThumbSelected{0, 422, 14, 11};

// ------------------------------------------------------------------ TEXT.BMP
inline constexpr int kCharW = 5;
inline constexpr int kCharH = 6;

// ------------------------------------------------------------------ main window layout
namespace main {
inline constexpr QSize kSize{275, 116};
inline constexpr QRect kTitleBarArea{0, 0, 275, 14};
inline constexpr QPoint kOptions{6, 3};
inline constexpr QPoint kMinimize{244, 3};
inline constexpr QPoint kShade{254, 3};
inline constexpr QPoint kClose{264, 3};
inline constexpr QPoint kClutter{10, 22};
inline constexpr QPoint kPlayPause{26, 28};
inline constexpr QPoint kTime{39, 26};  // digits at +9, +21, +39, +51; minus at -1,+6
inline constexpr QRect kVisualizer{24, 43, 76, 16};
inline constexpr QRect kMarquee{111, 27, 155, 6};
inline constexpr QPoint kKbps{111, 43};
inline constexpr QPoint kKhz{156, 43};
inline constexpr QPoint kMono{212, 41};
inline constexpr QPoint kStereo{239, 41};
inline constexpr QRect kVolume{107, 57, 68, 13};
inline constexpr QRect kBalance{177, 57, 38, 13};
inline constexpr QPoint kEqButton{219, 58};
inline constexpr QPoint kPlButton{242, 58};
inline constexpr QRect kPosition{16, 72, 248, 10};
inline constexpr QPoint kPrevious{16, 88};
inline constexpr QPoint kPlay{39, 88};
inline constexpr QPoint kPause{62, 88};
inline constexpr QPoint kStop{85, 88};
inline constexpr QPoint kNext{108, 88};
inline constexpr QPoint kEject{136, 89};
inline constexpr QPoint kShuffle{164, 89};
inline constexpr QPoint kRepeat{210, 89};
}  // namespace main

}  // namespace qiyaa::sprites
