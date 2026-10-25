#pragma once
#ifndef M5OS_QUAKE_HIGHRES
#define M5OS_QUAKE_HIGHRES 0
#endif
#if M5OS_QUAKE_HIGHRES
#define QV_MODE_NAME "512x300"
#define QV_WIDTH 512
#define QV_HEIGHT 300
#define QV_REPEAT 2
#define QV_OUT_WIDTH 1024
#define QV_OUT_HEIGHT 600
#define QV_X 128
#define QV_Y 60
#else
#define QV_MODE_NAME "320x200"
#define QV_WIDTH 320
#define QV_HEIGHT 200
#define QV_REPEAT 3
#define QV_OUT_WIDTH 960
#define QV_OUT_HEIGHT 720
#define QV_X 160
#define QV_Y 0
#endif
#define QV_PIXELS (QV_WIDTH*QV_HEIGHT)
