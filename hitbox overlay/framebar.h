#pragma once
#include <d3d9.h>
#include <d3dx9.h>

extern LPD3DXFONT m_font;

enum FrameState {
    FS_IDLE,
    FS_STARTUP,
    FS_ACTIVE,
    FS_RECOVERY,
    FS_BLOCKSTUN,
    FS_HITSTUN,
    FS_JUMP,
    FS_SHIELD,
    FS_WAKEUP,
    FS_BUNKER,
    FS_ARMOR,
    FS_NONE
};

struct FrameInfo {
    FrameState state;
    bool isHitstop;
    bool isInvincible;
    bool isProjectile; // Optional
    int duration;
};

struct MoveStats {
    int startup;
    int active;
    int recovery;
};

constexpr int FRAME_SEGMENTS = 90; // 1.5 seconds of frame data

struct PlayerFrameData {
    FrameInfo segments[FRAME_SEGMENTS];
    int current_idx;
    int current_state_time;
    FrameState current_state;
    DWORD last_health;
    WORD last_motion_type;
    bool is_hit_stun;
    bool has_attacked;
    MoveStats stats;
    int idle_time;

    void push(FrameInfo next) {
        if (next.state == FS_IDLE) {
            idle_time++;
        } else {
            idle_time = 0;
        }

        if (next.state == current_state && next.state != FS_NONE) {
            current_state_time++;
        } else {
            current_state = next.state;
            current_state_time = 1;
        }

        if (current_idx < FRAME_SEGMENTS - 1 || current_state_time == 1) {
            if (segments[current_idx].state != FS_NONE || current_idx > 0) {
                if (current_idx < FRAME_SEGMENTS - 1) {
                    current_idx++;
                } else {
                    // Scroll left
                    for (int i = 0; i < FRAME_SEGMENTS - 1; i++) {
                        segments[i] = segments[i + 1];
                    }
                }
            }
        } else if (current_idx == FRAME_SEGMENTS - 1) {
             // Scroll left
             for (int i = 0; i < FRAME_SEGMENTS - 1; i++) {
                 segments[i] = segments[i + 1];
             }
        }

        segments[current_idx] = next;
        segments[current_idx].duration = current_state_time;

        if (next.state == FS_STARTUP) stats.startup++;
        else if (next.state == FS_ACTIVE) stats.active++;
        else if (next.state == FS_RECOVERY) stats.recovery++;
    }

    void reset() {
        current_idx = 0;
        current_state = FS_NONE;
        current_state_time = 0;
        idle_time = 0;
        stats = {0, 0, 0};
        last_motion_type = 0;
        is_hit_stun = false;
        has_attacked = false;
        for (int i = 0; i < FRAME_SEGMENTS; ++i) {
            segments[i] = { FS_NONE, false, false, false, 0 };
        }
    }
};

extern PlayerFrameData p1_frames;
extern PlayerFrameData p2_frames;
extern int g_advantage;

extern bool g_framebar_toggled;

// Required pointers from dllmain.cpp
extern DWORD base_address;
extern DWORD p1_address;

// Function declarations
void InitFrameBar();
void UpdateFrameData();
void DrawRectangle(IDirect3DDevice9* pDevice, const float x1, const float x2, const float y1, const float y2, const float z, D3DCOLOR innerColor, D3DCOLOR outerColor);
void DrawFrameBar(IDirect3DDevice9* pDevice, int resX, int resY);
