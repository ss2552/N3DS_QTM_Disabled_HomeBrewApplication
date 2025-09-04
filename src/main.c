#include <3ds/types.h>
#include <3ds/console.h>
#include <3ds/gfx.h>
#include <3ds/services/hid.h>
#include <3ds/services/apt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>

void remote_play_DoQTMPatch(void);
void print(char *msg, ...);

// https://rgbcolorpicker.com/0-1
#define ERROR_COLOR 0b1001
#define SUCCESS_COLOR 0b1010
#define WHITE_COLOR 0b1111

// ? b g r
// 0b0111 灰色
// 0b0101 紫色

bool qtmDisabled = 0;
PrintConsole topScreenConsole;

int main(void)
{
    gfxInitDefault();
    consoleInit(GFX_TOP, &topScreenConsole);
    topScreenConsole.bg = ERROR_COLOR;
    topScreenConsole.fg = WHITE_COLOR;

    consoleClear();

    hidScanInput();
    bool fake_success_screen_show_key = hidKeysHeld() & KEY_A;
    if (!fake_success_screen_show_key)
        remote_play_DoQTMPatch();
    else
    {
        qtmDisabled = 1;
    }

    // qtmを無効にしたら
    if (qtmDisabled)
    {
        topScreenConsole.bg = SUCCESS_COLOR;
        consoleClear();
        print("QTM: Disabled Success");
    }

    if (fake_success_screen_show_key)
        print("Fake Success");
    print(" ");

    print("Exit: START Button");

    while (aptMainLoop())
    {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
        {
            break;
        }
    }
    consoleClear();
    gfxExit();
    return 0;
}

static u8 y = 0;

void print(char *msg, ...)
{
    va_list args;
    char s[100] = {0};
    va_start(args, msg);
    vsprintf(s, msg, args);
    printf("\x1b[%u;1H %s", ++y, s);
    va_end(args);
}
