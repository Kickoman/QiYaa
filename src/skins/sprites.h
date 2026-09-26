// Sprite coordinates inside Winamp 2.x skin bitmaps and element positions in the
// main window. Values taken from webamp (skinSprites.ts, css/main-window.css),
// MIT, (c) Jordan Eldredge — see THIRD_PARTY.md.
#pragma once

#include <QPoint>
#include <QRect>

namespace Skins {

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
inline constexpr QRect DigitSprite(int d) {
    return {d * kDigitW, 0, kDigitW, kDigitH};
}
inline constexpr QRect kMinusSign{20, 6, 5, 1};  // numbers.bmp
inline constexpr QRect kMinusSignEx{99, 0, 9, 13};  // nums_ex.bmp

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
inline constexpr ToggleSprite
    kShuffle{{28, 0, 47, 15}, {28, 15, 47, 15}, {28, 30, 47, 15}, {28, 45, 47, 15}};
inline constexpr ToggleSprite
    kRepeat{{0, 0, 28, 15}, {0, 15, 28, 15}, {0, 30, 28, 15}, {0, 45, 28, 15}};
inline constexpr ToggleSprite
    kEqButton{{0, 61, 23, 12}, {46, 61, 23, 12}, {0, 73, 23, 12}, {46, 73, 23, 12}};
inline constexpr ToggleSprite
    kPlButton{{23, 61, 23, 12}, {69, 61, 23, 12}, {23, 73, 23, 12}, {69, 73, 23, 12}};

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
struct EqualizerSprites {
    static constexpr QSize kSize{275, 116};
    static constexpr QRect kBackground{0, 0, 275, 116};
    static constexpr QRect kTitleBar{0, 149, 275, 14};
    static constexpr QRect kTitleBarSelected{0, 134, 275, 14};
    // Slider backgrounds: 28 frames (value low -> high), 14 per row, 15 px apart, rows 65 px apart.
    static constexpr QPoint kSliderFrames{13, 164};
    static constexpr QSize kSliderSize{14, 63};
    static constexpr QRect kThumb{0, 164, 11, 11};
    static constexpr QRect kThumbSelected{0, 176, 11, 11};
    static constexpr QRect kCloseButton{0, 116, 9, 9};
    static constexpr QRect kCloseButtonDown{0, 125, 9, 9};
    static constexpr ToggleSprite
        kOn{{10, 119, 26, 12}, {128, 119, 26, 12}, {69, 119, 26, 12}, {187, 119, 26, 12}};
    static constexpr ToggleSprite
        kAuto{{36, 119, 32, 12}, {154, 119, 32, 12}, {95, 119, 32, 12}, {213, 119, 32, 12}};
    static constexpr QRect kGraphBackground{0, 294, 113, 19};
    static constexpr QRect kGraphLineColors{115, 294, 1, 19};
    static constexpr QRect kPreampLine{0, 314, 113, 1};
    static constexpr QRect kPresetsButton{224, 164, 44, 12};
    static constexpr QRect kPresetsButtonSelected{224, 176, 44, 12};

    // Layout.
    static constexpr QPoint kClose{264, 3};
    static constexpr QPoint kOnPos{14, 18};
    static constexpr QPoint kAutoPos{40, 18};
    static constexpr QPoint kPresetsPos{217, 18};
    static constexpr QPoint kGraphPos{86, 17};
    static constexpr QPoint kPreampPos{21, 38};
    static constexpr int kBandsX = 78;  // first band
    static constexpr int kBandStep = 18;
    static constexpr int kSlidersY = 38;
    static constexpr int kSliderTravel = 62 - 11;  // thumb travel in px
};

// ------------------------------------------------------------------ EQ_EX.BMP (equalizer shade)
struct EqualizerShadeSprites {
    static constexpr QRect kShadeBackgroundSelected{0, 0, 275, 14};
    static constexpr QRect kShadeBackground{0, 15, 275, 14};
    static constexpr QRect kVolumeThumb[3] =
        {{1, 30, 3, 7}, {4, 30, 3, 7}, {7, 30, 3, 7}};  // left/centre/right
    static constexpr QRect kBalanceThumb[3] = {{11, 30, 3, 7}, {14, 30, 3, 7}, {17, 30, 3, 7}};
    static constexpr QRect kShadeButtonDown{
        1, 38, 9, 9
    };  // normal mode, pressed ("maximize" in webamp)
    static constexpr QRect kShadeButtonShadedDown{1, 47, 9, 9};  // shade mode, pressed ("minimize")
    static constexpr QRect kCloseButtonDown{11, 47, 9, 9};
    static constexpr QRect kVolume{61, 4, 97, 7};
    static constexpr QRect kBalance{164, 4, 43, 7};
};

// ------------------------------------------------------------------ PLEDIT.BMP
struct PlaylistSprites {
    static constexpr QRect kTopTile{127, 21, 25, 20};
    static constexpr QRect kTopLeft{0, 21, 25, 20};
    static constexpr QRect kTitle{26, 21, 100, 20};
    static constexpr QRect kTopRight{153, 21, 25, 20};
    static constexpr QRect kTopTileSelected{127, 0, 25, 20};
    static constexpr QRect kTopLeftSelected{0, 0, 25, 20};
    static constexpr QRect kTitleSelected{26, 0, 100, 20};
    static constexpr QRect kTopRightSelected{153, 0, 25, 20};
    static constexpr QRect kLeftTile{0, 42, 12, 29};
    static constexpr QRect kRightTile{31, 42, 20, 29};
    static constexpr QRect kBottomTile{179, 0, 25, 38};
    static constexpr QRect kBottomLeft{0, 72, 125, 38};
    static constexpr QRect kBottomRight{126, 72, 150, 38};
    static constexpr QRect kScrollHandle{52, 53, 8, 18};
    static constexpr QRect kScrollHandleSelected{61, 53, 8, 18};
    static constexpr QRect kCloseSelected{52, 42, 9, 9};
    static constexpr QRect kCollapseSelected{62, 42, 9, 9};
    static constexpr QRect kExpandSelected{150, 42, 9, 9};
    static constexpr QRect kShadeTile{72, 57, 25, 14};
    static constexpr QRect kShadeLeft{72, 42, 25, 14};
    static constexpr QRect kShadeRight{99, 57, 50, 14};
    static constexpr QRect kShadeRightSelected{99, 42, 50, 14};

    static constexpr QSize kMinSize{275, 116};
    static constexpr int kStepW = 25;
    static constexpr int kStepH = 29;
    static constexpr int kTopH = 20;
    static constexpr int kBottomH = 38;
    static constexpr int kLeftW = 12;
    static constexpr int kRightW = 20;
    static constexpr int kRowH = 13;
};

// ------------------------------------------------------------------ GEN.BMP (generic windows)
struct GenWindowSprites {
    static constexpr QRect kTopLeftSelected{0, 0, 25, 20};
    static constexpr QRect kTopLeftEndSelected{26, 0, 25, 20};
    static constexpr QRect kTopCenterFillSelected{52, 0, 25, 20};
    static constexpr QRect kTopRightEndSelected{78, 0, 25, 20};
    static constexpr QRect kTopLeftRightFillSelected{104, 0, 25, 20};
    static constexpr QRect kTopRightSelected{130, 0, 25, 20};
    static constexpr QRect kTopLeft{0, 21, 25, 20};
    static constexpr QRect kTopLeftEnd{26, 21, 25, 20};
    static constexpr QRect kTopCenterFill{52, 21, 25, 20};
    static constexpr QRect kTopRightEnd{78, 21, 25, 20};
    static constexpr QRect kTopLeftRightFill{104, 21, 25, 20};
    static constexpr QRect kTopRight{130, 21, 25, 20};
    static constexpr QRect kBottomLeft{0, 42, 125, 14};
    static constexpr QRect kBottomRight{0, 57, 125, 14};
    static constexpr QRect kBottomFill{127, 72, 25, 14};
    static constexpr QRect kMiddleLeft{127, 42, 11, 29};
    static constexpr QRect kMiddleLeftBottom{158, 42, 11, 24};
    static constexpr QRect kMiddleRight{139, 42, 8, 29};
    static constexpr QRect kMiddleRightBottom{170, 42, 8, 24};
    static constexpr QRect kCloseSelected{148, 42, 9, 9};
    static constexpr int kLettersYSelected = 88;
    static constexpr int kLettersY = 96;
    static constexpr int kLetterH = 7;
};

// ------------------------------------------------------------------ main window layout
struct MainWindowSprites {
    static constexpr QSize kSize{275, 116};
    static constexpr QRect kTitleBarArea{0, 0, 275, 14};
    static constexpr QPoint kOptions{6, 3};
    static constexpr QPoint kMinimize{244, 3};
    static constexpr QPoint kShade{254, 3};
    static constexpr QPoint kClose{264, 3};
    static constexpr QPoint kClutter{10, 22};
    static constexpr QPoint kPlayPause{26, 28};
    static constexpr QPoint kTime{39, 26};  // digits at +9, +21, +39, +51; minus at -1,+6
    static constexpr QRect kVisualizer{24, 43, 76, 16};
    static constexpr QRect kMarquee{111, 27, 155, 6};
    static constexpr QPoint kKbps{111, 43};
    static constexpr QPoint kKhz{156, 43};
    static constexpr QPoint kMono{212, 41};
    static constexpr QPoint kStereo{239, 41};
    static constexpr QRect kVolume{107, 57, 68, 13};
    static constexpr QRect kBalance{177, 57, 38, 13};
    static constexpr QPoint kEqButton{219, 58};
    static constexpr QPoint kPlButton{242, 58};
    static constexpr QRect kPosition{16, 72, 248, 10};
    static constexpr QPoint kPrevious{16, 88};
    static constexpr QPoint kPlay{39, 88};
    static constexpr QPoint kPause{62, 88};
    static constexpr QPoint kStop{85, 88};
    static constexpr QPoint kNext{108, 88};
    static constexpr QPoint kEject{136, 89};
    static constexpr QPoint kShuffle{164, 89};
    static constexpr QPoint kRepeat{210, 89};
};

}  // namespace Skins
