#include <Arduino.h>
#include "Arduino_LED_Matrix.h"

namespace
{
    constexpr int MatrixWidth = 12;
    constexpr int MatrixHeight = 8;
    constexpr int FrameCount = 8;
    constexpr unsigned long FrameDurationMs = 100;

    ArduinoLEDMatrix Matrix;
    uint32_t Frames[2][FrameCount][4] = {};
    int ActiveBuffer = 0;
    bool AlternatePattern = false;

    void buildSequence(int bufferIndex, bool alternate)
    {
        uint8_t pixels[MatrixHeight][MatrixWidth];
        for (int frameIndex = 0; frameIndex < FrameCount; ++frameIndex)
        {
            memset(pixels, 0, sizeof(pixels));

            const int movingX = alternate
                ? MatrixWidth - 1 - frameIndex
                : frameIndex;
            const int movingY = alternate ? 5 : 2;
            pixels[movingY][movingX] = 1;
            pixels[3][5] = 1;
            pixels[4][6] = 1;

            ArduinoLEDMatrix::loadPixelsToBuffer(
                &pixels[0][0],
                sizeof(pixels),
                Frames[bufferIndex][frameIndex]);
            Frames[bufferIndex][frameIndex][3] = FrameDurationMs;
        }
    }

    void publishSequence(int bufferIndex)
    {
        noInterrupts();
        Matrix.loadSequence(Frames[bufferIndex]);
        Matrix.play(true);
        ActiveBuffer = bufferIndex;
        interrupts();
    }
}

void setup()
{
    Matrix.begin();
    buildSequence(ActiveBuffer, AlternatePattern);
    publishSequence(ActiveBuffer);
}

void loop()
{
    // Simulate a long synchronous network operation. The timer-driven matrix
    // animation must continue smoothly throughout this delay.
    delay(2500);

    const int nextBuffer = 1 - ActiveBuffer;
    AlternatePattern = !AlternatePattern;
    buildSequence(nextBuffer, AlternatePattern);
    publishSequence(nextBuffer);
}
