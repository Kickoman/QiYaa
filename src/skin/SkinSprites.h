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
// Shade ("windowshade") mode of the main window.
inline constexpr QRect kShadeBackground{27, 42, 275, 14};
inline constexpr QRect kShadeBackgroundSelected{27, 29, 275, 14};
inline constexpr QRect kShadeButtonShaded{0, 27, 9, 9};  // shade button while shaded
inline constexpr QRect kShadeButtonShadedDown{9, 27, 9, 9};
inline constexpr QRect kShadePositionBackground{0, 36, 17, 7};
inline constexpr QRect kShadePositionThumb{20, 36, 3, 7};
inline constexpr QRect kShadePositionThumbLeft{17, 36, 3, 7};
inline constexpr QRect kShadePositionThumbRight{23, 36, 3, 7};

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

// ------------------------------------------------------------------ EQMAIN.BMP
namespace eq {
inline constexpr QSize kSize{275, 116};
inline constexpr QRect kBackground{0, 0, 275, 116};
inline constexpr QRect kTitleBar{0, 149, 275, 14};
inline constexpr QRect kTitleBarSelected{0, 134, 275, 14};
// Slider backgrounds: 28 frames (value low -> high), 14 per row, 15 px apart, rows 65 px apart.
inline constexpr QPoint kSliderFrames{13, 164};
inline constexpr QSize kSliderSize{14, 63};
inline constexpr QRect kThumb{0, 164, 11, 11};
inline constexpr QRect kThumbSelected{0, 176, 11, 11};
inline constexpr QRect kCloseButton{0, 116, 9, 9};
inline constexpr QRect kCloseButtonDown{0, 125, 9, 9};
inline constexpr ToggleSprite kOn{{10, 119, 26, 12}, {128, 119, 26, 12}, {69, 119, 26, 12}, {187, 119, 26, 12}};
inline constexpr ToggleSprite kAuto{{36, 119, 32, 12}, {154, 119, 32, 12}, {95, 119, 32, 12}, {213, 119, 32, 12}};
inline constexpr QRect kGraphBackground{0, 294, 113, 19};
inline constexpr QRect kGraphLineColors{115, 294, 1, 19};
inline constexpr QRect kPreampLine{0, 314, 113, 1};
inline constexpr QRect kPresetsButton{224, 164, 44, 12};
inline constexpr QRect kPresetsButtonSelected{224, 176, 44, 12};

// Layout.
inline constexpr QPoint kClose{264, 3};
inline constexpr QPoint kOnPos{14, 18};
inline constexpr QPoint kAutoPos{40, 18};
inline constexpr QPoint kPresetsPos{217, 18};
inline constexpr QPoint kGraphPos{86, 17};
inline constexpr QPoint kPreampPos{21, 38};
inline constexpr int kBandsX = 78;     // first band
inline constexpr int kBandStep = 18;
inline constexpr int kSlidersY = 38;
inline constexpr int kSliderTravel = 62 - 11;  // thumb travel in px
}  // namespace eq

// ------------------------------------------------------------------ EQ_EX.BMP (equalizer shade)
namespace eqex {
inline constexpr QRect kShadeBackgroundSelected{0, 0, 275, 14};
inline constexpr QRect kShadeBackground{0, 15, 275, 14};
inline constexpr QRect kVolumeThumb[3] = {{1, 30, 3, 7}, {4, 30, 3, 7}, {7, 30, 3, 7}};   // left/centre/right
inline constexpr QRect kBalanceThumb[3] = {{11, 30, 3, 7}, {14, 30, 3, 7}, {17, 30, 3, 7}};
inline constexpr QRect kShadeButtonDown{1, 38, 9, 9};        // normal mode, pressed ("maximize" in webamp)
inline constexpr QRect kShadeButtonShadedDown{1, 47, 9, 9};  // shade mode, pressed ("minimize")
inline constexpr QRect kCloseButtonDown{11, 47, 9, 9};
inline constexpr QRect kVolume{61, 4, 97, 7};
inline constexpr QRect kBalance{164, 4, 43, 7};
}  // namespace eqex

// ------------------------------------------------------------------ PLEDIT.BMP
namespace pl {
inline constexpr QRect kTopTile{127, 21, 25, 20};
inline constexpr QRect kTopLeft{0, 21, 25, 20};
inline constexpr QRect kTitle{26, 21, 100, 20};
inline constexpr QRect kTopRight{153, 21, 25, 20};
inline constexpr QRect kTopTileSelected{127, 0, 25, 20};
inline constexpr QRect kTopLeftSelected{0, 0, 25, 20};
inline constexpr QRect kTitleSelected{26, 0, 100, 20};
inline constexpr QRect kTopRightSelected{153, 0, 25, 20};
inline constexpr QRect kLeftTile{0, 42, 12, 29};
inline constexpr QRect kRightTile{31, 42, 20, 29};
inline constexpr QRect kBottomTile{179, 0, 25, 38};
inline constexpr QRect kBottomLeft{0, 72, 125, 38};
inline constexpr QRect kBottomRight{126, 72, 150, 38};
inline constexpr QRect kScrollHandle{52, 53, 8, 18};
inline constexpr QRect kScrollHandleSelected{61, 53, 8, 18};
inline constexpr QRect kCloseSelected{52, 42, 9, 9};
inline constexpr QRect kCollapseSelected{62, 42, 9, 9};
inline constexpr QRect kExpandSelected{150, 42, 9, 9};
inline constexpr QRect kShadeTile{72, 57, 25, 14};
inline constexpr QRect kShadeLeft{72, 42, 25, 14};
inline constexpr QRect kShadeRight{99, 57, 50, 14};
inline constexpr QRect kShadeRightSelected{99, 42, 50, 14};

inline constexpr QSize kMinSize{275, 116};
inline constexpr int kStepW = 25;
inline constexpr int kStepH = 29;
inline constexpr int kTopH = 20;
inline constexpr int kBottomH = 38;
inline constexpr int kLeftW = 12;
inline constexpr int kRightW = 20;
inline constexpr int kRowH = 13;
}  // namespace pl

// ------------------------------------------------------------------ GEN.BMP (generic windows)
namespace gen {
inline constexpr QRect kTopLeftSelected{0, 0, 25, 20};
inline constexpr QRect kTopLeftEndSelected{26, 0, 25, 20};
inline constexpr QRect kTopCenterFillSelected{52, 0, 25, 20};
inline constexpr QRect kTopRightEndSelected{78, 0, 25, 20};
inline constexpr QRect kTopLeftRightFillSelected{104, 0, 25, 20};
inline constexpr QRect kTopRightSelected{130, 0, 25, 20};
inline constexpr QRect kTopLeft{0, 21, 25, 20};
inline constexpr QRect kTopLeftEnd{26, 21, 25, 20};
inline constexpr QRect kTopCenterFill{52, 21, 25, 20};
inline constexpr QRect kTopRightEnd{78, 21, 25, 20};
inline constexpr QRect kTopLeftRightFill{104, 21, 25, 20};
inline constexpr QRect kTopRight{130, 21, 25, 20};
inline constexpr QRect kBottomLeft{0, 42, 125, 14};
inline constexpr QRect kBottomRight{0, 57, 125, 14};
inline constexpr QRect kBottomFill{127, 72, 25, 14};
inline constexpr QRect kMiddleLeft{127, 42, 11, 29};
inline constexpr QRect kMiddleLeftBottom{158, 42, 11, 24};
inline constexpr QRect kMiddleRight{139, 42, 8, 29};
inline constexpr QRect kMiddleRightBottom{170, 42, 8, 24};
inline constexpr QRect kCloseSelected{148, 42, 9, 9};
inline constexpr int kLettersYSelected = 88;
inline constexpr int kLettersY = 96;
inline constexpr int kLetterH = 7;
}  // namespace gen

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
