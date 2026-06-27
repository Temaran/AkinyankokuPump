#include "FirmwareServices.h"

#include <string.h>

#include "GardenLogic.h"

namespace GardenPump
{
    ArduinoLEDMatrix LedMatrix;

    namespace
    {
        constexpr int kMatrixWidth = 12;
        constexpr int kMatrixHeight = 8;
        constexpr unsigned long kWateringAnimationRevolutionMs = 1000;
        constexpr unsigned long kWateringAnimationFrameMs =
            kWateringAnimationRevolutionMs / GardenWateringAnimLength;
        uint32_t IrrigationAnimationFrames[2][GardenWateringAnimLength][4] = {};
        int ActiveIrrigationAnimationBuffer = 0;
        uint16_t LastIrrigationVisualState = 0;
        bool IrrigationDisplayInitialized = false;

        constexpr uint8_t kFontDigits[10][5] = {
            {0b111, 0b101, 0b101, 0b101, 0b111},
            {0b010, 0b110, 0b010, 0b010, 0b111},
            {0b111, 0b001, 0b111, 0b100, 0b111},
            {0b111, 0b001, 0b111, 0b001, 0b111},
            {0b101, 0b101, 0b111, 0b001, 0b001},
            {0b111, 0b100, 0b111, 0b001, 0b111},
            {0b111, 0b100, 0b111, 0b101, 0b111},
            {0b111, 0b001, 0b001, 0b001, 0b001},
            {0b111, 0b101, 0b111, 0b101, 0b111},
            {0b111, 0b101, 0b111, 0b001, 0b111},
        };
        constexpr uint8_t kFontC[5] = {0b111, 0b100, 0b100, 0b100, 0b111};
        constexpr uint8_t kFontH[5] = {0b101, 0b101, 0b111, 0b101, 0b101};
        constexpr uint8_t kFontL[5] = {0b100, 0b100, 0b100, 0b100, 0b111};
        constexpr uint8_t kFontM[5] = {0b101, 0b111, 0b111, 0b101, 0b101};
        constexpr uint8_t kFontO[5] = {0b111, 0b101, 0b101, 0b101, 0b111};
        constexpr uint8_t kFontI[5] = {0b111, 0b010, 0b010, 0b010, 0b111};
        constexpr uint8_t kFontR[5] = {0b110, 0b101, 0b110, 0b101, 0b101};
        constexpr uint8_t kFontG[5] = {0b111, 0b100, 0b101, 0b101, 0b111};
        constexpr uint8_t kFontD[5] = {0b110, 0b101, 0b101, 0b101, 0b110};
        constexpr uint8_t kFontP[5] = {0b110, 0b101, 0b110, 0b100, 0b100};

        uint8_t irrigationVisualState(const GardenCell& sourceCell)
        {
            uint8_t state = 0;
            if (!sourceCell.HasConnected())
            {
                state = 0;
            }
            else if (sourceCell.HasError())
            {
                state = 1;
            }
            else if (sourceCell.ShouldWater())
            {
                state = 2;
            }
            else
            {
                state = 3;
            }

            const int moistureDots =
                sourceCell.HasConnected() && !sourceCell.HasError()
                    ? GardenLogic::MoistureDotCount(
                          sourceCell.GetMoistnessNorm(),
                          sourceCell.GetStartWateringThresholdNorm(),
                          sourceCell.GetStopWateringThresholdNorm())
                    : 0;
            return static_cast<uint8_t>(state | (moistureDots << 2));
        }

        uint16_t currentIrrigationVisualState()
        {
            uint16_t signature = 0;
            for (int zoneIdx = 0; zoneIdx < NrCells; ++zoneIdx)
            {
                const int sensorIdx =
                    GardenLogic::CoerceZoneSensor(zoneIdx, Config.zoneSensor[zoneIdx]);
                signature |= static_cast<uint16_t>(
                    irrigationVisualState(Cells[sensorIdx]) << (zoneIdx * 4));
            }
            return signature;
        }
    }
}
namespace GardenPump
{
    void initializeLedStatusDisplay()
    {
        LedMatrix.begin();
    }

    void playStartupDisplayAnimation()
    {
        noInterrupts();
        LedMatrix.loadSequence(LEDMATRIX_ANIMATION_STARTUP);
        LedMatrix.play(true);
        interrupts();
    }

    void invalidateIrrigationDisplayAnimation()
    {
        IrrigationDisplayInitialized = false;
    }

    void stopIrrigationDisplayAnimation()
    {
        noInterrupts();
        LedMatrix.clear();
        interrupts();
        IrrigationDisplayInitialized = false;
    }

    void updateIrrigationDisplayAnimation()
    {
        const uint16_t visualState = currentIrrigationVisualState();
        if (IrrigationDisplayInitialized && visualState == LastIrrigationVisualState)
        {
            return;
        }

        const int nextBuffer = 1 - ActiveIrrigationAnimationBuffer;
        uint8_t pixels[kMatrixHeight][kMatrixWidth];
        for (int frameIdx = 0; frameIdx < GardenWateringAnimLength; ++frameIdx)
        {
            memset(pixels, 0, sizeof(pixels));
            for (int zoneIdx = 0; zoneIdx < NrCells; ++zoneIdx)
            {
                const int sensorIdx =
                    GardenLogic::CoerceZoneSensor(zoneIdx, Config.zoneSensor[zoneIdx]);
                Cells[zoneIdx].RenderFrameToBuffer(
                    &pixels[0][0],
                    kMatrixWidth,
                    kMatrixHeight,
                    Cells[sensorIdx],
                    frameIdx);
            }

            ArduinoLEDMatrix::loadPixelsToBuffer(
                &pixels[0][0],
                sizeof(pixels),
                IrrigationAnimationFrames[nextBuffer][frameIdx]);
            IrrigationAnimationFrames[nextBuffer][frameIdx][3] =
                kWateringAnimationFrameMs;
        }

        noInterrupts();
        LedMatrix.loadSequence(IrrigationAnimationFrames[nextBuffer]);
        LedMatrix.play(true);
        ActiveIrrigationAnimationBuffer = nextBuffer;
        interrupts();

        LastIrrigationVisualState = visualState;
        IrrigationDisplayInitialized = true;
    }

    void writePixel(int x, int y, bool on)
    {
        const uint8_t value = on ? 255 : 0;
        LedMatrix.set(x, y, value, value, value);
    }

    void clearMatrix()
    {
        for (int x = 0; x < 12; ++x)
        {
            for (int y = 0; y < 8; ++y)
            {
                writePixel(x, y, false);
            }
        }
    }

    void drawGlyph3x5(int startX, int startY, const uint8_t glyph[5])
    {
        for (int row = 0; row < 5; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                const bool on = (glyph[row] & (1 << (2 - col))) != 0;
                writePixel(startX + col, startY + row, on);
            }
        }
    }

    void drawDigit3x5(int startX, int startY, int digit)
    {
        digit = constrain(digit, 0, 9);
        drawGlyph3x5(startX, startY, kFontDigits[digit]);
    }

    void drawCountScreen(uint32_t count)
    {
        clearMatrix();

        const uint32_t shown = count % 1000UL;
        const int hundreds = static_cast<int>((shown / 100UL) % 10UL);
        const int tens = static_cast<int>((shown / 10UL) % 10UL);
        const int ones = static_cast<int>(shown % 10UL);

        drawDigit3x5(0, 1, hundreds);
        drawDigit3x5(4, 1, tens);
        drawDigit3x5(8, 1, ones);

        if (count >= 1000UL)
        {
            writePixel(11, 0, true);
        }
    }

    void drawBarScreen(const uint8_t label[5], const uint8_t values[NrCells])
    {
        clearMatrix();
        drawGlyph3x5(0, 1, label);

        for (int cellIdx = 0; cellIdx < NrCells; ++cellIdx)
        {
            const int barX = 4 + cellIdx * 2;
            const int barHeight = map(values[cellIdx], 0, 254, 0, 5);
            for (int y = 0; y < barHeight; ++y)
            {
                writePixel(barX, 5 - y, true);
            }
        }
    }

    void drawText3x5(const uint8_t left[5], const uint8_t middle[5], const uint8_t right[5])
    {
        clearMatrix();
        drawGlyph3x5(0, 1, left);
        drawGlyph3x5(4, 1, middle);
        drawGlyph3x5(8, 1, right);
    }

    void renderGatherModeDisplay()
    {
        LedMatrix.beginDraw();
        if (DataState.outOfMemory)
        {
            drawText3x5(kFontO, kFontO, kFontM);
            LedMatrix.endDraw();
            return;
        }

        if ((millis() - DataState.lastStatsScreenSwapMs) >= StatsScreenPeriodMs)
        {
            DataState.currentStatsScreen = (DataState.currentStatsScreen + 1) % 3;
            DataState.lastStatsScreenSwapMs = millis();
        }

        switch (DataState.currentStatsScreen)
        {
            case 0:
                drawCountScreen(DataState.totalSamples);
                break;
            case 1:
                drawBarScreen(kFontH, DataState.maxValues);
                break;
            case 2:
            default:
                drawBarScreen(kFontL, DataState.minValues);
                break;
        }
        LedMatrix.endDraw();
    }

    void renderDumpDisplay()
    {
        LedMatrix.beginDraw();
        drawText3x5(kFontD, kFontM, kFontP);
        LedMatrix.endDraw();
    }
}
