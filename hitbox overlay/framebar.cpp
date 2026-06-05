#include "pch.h"
#include "framebar.h"
#include <string>

PlayerFrameData p1_frames;
PlayerFrameData p2_frames;
int g_advantage = 0;
bool g_framebar_toggled = true;

// Offsets and sizes
#define TIMER_ADDRESS 0x8A9C58
#define TR_FLAG_ADDRESS 0x874874
#define PLR_STRUCT_SIZE 0xC44

static DWORD last_timer = 0;

FrameInfo GetFrameInfo(DWORD playerAddr, PlayerFrameData& p_data) {
    FrameInfo info = { FS_IDLE, false, false, false, 0 };

    if (!playerAddr) return info;

    WORD motion_type = *(WORD*)(uintptr_t)(playerAddr + 0x1C);
    DWORD motion = *(DWORD*)(uintptr_t)(playerAddr + 0x570);
    BYTE atk = *(BYTE*)(uintptr_t)(playerAddr + 0x60);
    BYTE inv = *(BYTE*)(uintptr_t)(playerAddr + 0x61);
    BYTE hitstop = *(BYTE*)(uintptr_t)(playerAddr + 0x298);
    BYTE seeld = *(BYTE*)(uintptr_t)(playerAddr + 0x2A0);
    WORD hit = *(WORD*)(uintptr_t)(playerAddr + 0x2D8);
    BYTE armor_1 = *(BYTE*)(uintptr_t)(playerAddr + 0x614);
    BYTE armor_2 = *(BYTE*)(uintptr_t)(playerAddr + 0xC0);
    DWORD health = *(DWORD*)(uintptr_t)(playerAddr + 0x8C);

    bool isHitstop = (hitstop != 0);
    bool isInvincible = (inv == 0 && motion != 0);
    bool isStun = (hit != 0 || motion_type == 620 || motion_type == 621 || motion_type == 624);
    bool isAtk = (atk != 0);

    if (motion_type != p_data.last_motion_type) {
        if (motion_type != 0 && !isStun) {
            p_data.stats = {0, 0, 0};
        }
        p_data.last_motion_type = motion_type;
    }

    info.isHitstop = isHitstop;
    info.isInvincible = isInvincible;

    if (isStun) {
        if (health < p_data.last_health) {
            p_data.is_hit_stun = true;
        }
        info.state = p_data.is_hit_stun ? FS_HITSTUN : FS_BLOCKSTUN;
    } else if (seeld == 2 || seeld == 3) {
        info.state = FS_SHIELD;
    } else if (armor_1 == 1 || armor_2 == 15) {
        info.state = FS_ARMOR;
    } else if (motion_type == 593 || motion_type == 594) {
        info.state = FS_WAKEUP;
    } else if (isAtk) {
        info.state = FS_ACTIVE;
        p_data.has_attacked = true;
    } else if (motion_type != 0 && motion != 0) {
        if (p_data.has_attacked) {
            info.state = FS_RECOVERY;
        } else {
            info.state = FS_STARTUP;
        }
    } else if (motion_type != 0 && motion == 0) {
        info.state = FS_RECOVERY;
    } else if (motion_type >= 34 && motion_type <= 40) {
        info.state = FS_JUMP;
    } else {
        info.state = FS_IDLE;
        p_data.is_hit_stun = false; // Reset when back to neutral
        p_data.has_attacked = false;
    }

    p_data.last_health = health;

    // Bunker check
    DWORD bunker_pointer = *(DWORD*)(uintptr_t)(playerAddr + 0x6EC);
    if (bunker_pointer) {
        // Safeguard to prevent reading invalid memory
        __try {
            BYTE actual_bunker = *(BYTE*)(uintptr_t)(bunker_pointer + 0xB6);
            if (actual_bunker == 12 && motion_type < 110) {
                info.state = FS_BUNKER;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Ignore
        }
    }

    return info;
}

bool g_in_match = false;
bool g_combo_active = false;

void UpdateFrameData() {
    if (!p1_address || !base_address) return;

    DWORD tr_flag = 0;
    __try {
        tr_flag = *(DWORD*)(uintptr_t)(base_address + TR_FLAG_ADDRESS);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
    }

    if (tr_flag == 300 || tr_flag == 103) {
        g_in_match = true;
    } else {
        g_in_match = false;
        p1_frames.reset();
        p2_frames.reset();
        g_combo_active = false;
        return;
    }

    // Use absolute address as in MBTL_Training
    DWORD current_timer = 0;
    __try {
        current_timer = *(DWORD*)(uintptr_t)(base_address + TIMER_ADDRESS);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return; // Failed to read timer
    }

    if (current_timer != last_timer) {
        last_timer = current_timer;
        
        FrameInfo p1_info = GetFrameInfo(p1_address, p1_frames);
        FrameInfo p2_info = GetFrameInfo(p1_address + PLR_STRUCT_SIZE, p2_frames);

        if (p1_info.state == FS_IDLE && p2_info.state == FS_IDLE) {
            if (!g_combo_active) {
                return;
            }
            if (p1_frames.idle_time > 20 && p2_frames.idle_time > 20) {
                g_combo_active = false;
                g_advantage = 0;
                return;
            }
        } else if (!g_combo_active) {
            g_combo_active = true;
            p1_frames.reset();
            p2_frames.reset();
            g_advantage = 0;
            // Need to populate last_health immediately upon reset to avoid instant hit_stun false positive
            p1_frames.last_health = *(DWORD*)(uintptr_t)(p1_address + 0x8C);
            p2_frames.last_health = *(DWORD*)(uintptr_t)(p1_address + PLR_STRUCT_SIZE + 0x8C);
        }
        
        p1_frames.push(p1_info);
        p2_frames.push(p2_info);

        // Update Advantage
        if (p1_info.state == FS_IDLE && p2_info.state != FS_IDLE) {
            g_advantage++;
        } else if (p2_info.state == FS_IDLE && p1_info.state != FS_IDLE) {
            g_advantage--;
        } else if (p1_info.state != FS_IDLE && p2_info.state != FS_IDLE) {
            g_advantage = 0;
        }
    }
}



void DrawTextD3D(const char* text, int x, int y, D3DCOLOR color, bool center = false, bool outline = true) {
    if (!m_font) return;
    
    auto Draw = [&](int ox, int oy, D3DCOLOR c) {
        RECT rect;
        SetRect(&rect, x + ox, y + oy, x + ox + 500, y + oy + 50);
        DWORD format = DT_LEFT | DT_NOCLIP;
        if (center) {
            format = DT_CENTER | DT_NOCLIP;
            SetRect(&rect, x + ox - 10, y + oy, x + ox + 10, y + oy + 50);
        }
        m_font->DrawTextA(NULL, text, -1, &rect, format, c);
    };

    if (outline) {
        D3DCOLOR outlineColor = D3DCOLOR_ARGB(255, 0, 0, 0);
        Draw(-1, 0, outlineColor);
        Draw(1, 0, outlineColor);
        Draw(0, -1, outlineColor);
        Draw(0, 1, outlineColor);
        // Diagonals for thicker outline
        Draw(-1, -1, outlineColor);
        Draw(1, 1, outlineColor);
        Draw(-1, 1, outlineColor);
        Draw(1, -1, outlineColor);
    }
    
    Draw(0, 0, color);
}

void DrawFrameBar(IDirect3DDevice9* pDevice, int resX, int resY) {
    if (!g_framebar_toggled || !g_in_match) return;

    // Layout config matching StriveFrameViewer spacing
    const float SEG_WIDTH = 12.0f;
    const float SEG_HEIGHT = 28.0f;
    const float SPACING = 2.0f;
    
    // Draw in the bottom center
    const float total_width = (FRAME_SEGMENTS * (SEG_WIDTH + SPACING)) - SPACING;
    const float start_x = (resX / 2.0f) - (total_width / 2.0f);
    const float start_y_p1 = resY - 140.0f;
    const float start_y_p2 = start_y_p1 + SEG_HEIGHT + 8.0f;

    auto DrawPlayerFrames = [&](PlayerFrameData& p_data, float y_offset, bool is_p1) {
        // Draw Stats Text
        char statsBuffer[256];
        sprintf_s(statsBuffer, "Startup: %d, Active: %d, Recovery: %d, Advantage: %d", 
            p_data.stats.startup, p_data.stats.active, p_data.stats.recovery, is_p1 ? g_advantage : -g_advantage);
        
        float text_y = is_p1 ? (y_offset - 25.0f) : (y_offset + SEG_HEIGHT + 5.0f);
        DrawTextD3D(statsBuffer, (int)start_x, (int)text_y, D3DCOLOR_ARGB(255, 255, 255, 255));

        // Draw background bar
        DrawRectangle(pDevice, start_x - 5.0f, start_x + total_width + 5.0f, 
                     y_offset - 5.0f, y_offset + SEG_HEIGHT + 5.0f, 0.0f, 
                     D3DCOLOR_ARGB(150, 0, 0, 0), D3DCOLOR_ARGB(255, 0, 0, 0));

        // Draw segments left-to-right
        for (int i = 0; i < FRAME_SEGMENTS; ++i) {
            FrameInfo info = p_data.segments[i];
            if (info.state == FS_NONE) break; // Reached end of recorded data

            float x = start_x + (i * (SEG_WIDTH + SPACING));
            
            // Map StriveFrameViewer Colors
            D3DCOLOR inner = D3DCOLOR_ARGB(180, 128, 128, 128);
            D3DCOLOR outer = D3DCOLOR_ARGB(255, 0, 0, 0);

            switch (info.state) {
            case FS_IDLE:      inner = D3DCOLOR_ARGB(200, 140, 140, 140); break; // Gray
            case FS_BLOCKSTUN: inner = D3DCOLOR_ARGB(200, 80, 120, 220); break; // Soft Blue
            case FS_HITSTUN:   inner = D3DCOLOR_ARGB(200, 100, 200, 100); break; // Soft Green
            case FS_STARTUP:
            case FS_WAKEUP:
            case FS_JUMP:      inner = D3DCOLOR_ARGB(200, 210, 210, 80); break; // Soft Yellow
            case FS_ACTIVE:    inner = D3DCOLOR_ARGB(200, 220, 100, 100); break; // Soft Red
            case FS_RECOVERY:  inner = D3DCOLOR_ARGB(200, 230, 160, 80); break; // Soft Orange
            case FS_SHIELD:    inner = D3DCOLOR_ARGB(200, 80, 200, 200); break; // Cyan (Custom)
            case FS_ARMOR:     
            case FS_BUNKER:    inner = D3DCOLOR_ARGB(200, 200, 80, 200); break; // Magenta (Custom)
            default: break;
            }

            if (info.isHitstop) {
                inner = D3DCOLOR_ARGB(200, 0, 0, 128); // Dark Blue for Hitstop
            }

            DrawRectangle(pDevice, x, x + SEG_WIDTH, y_offset, y_offset + SEG_HEIGHT, 0.0f, inner, outer);

            if (info.isInvincible) {
                DrawRectangle(pDevice, x, x + SEG_WIDTH, y_offset, y_offset + 5.0f, 0.0f, D3DCOLOR_ARGB(255, 255, 255, 255), outer);
            }
            
            // Draw duration number if this is the last segment of identical states, 
            // OR if it's currently the last written frame in the combo buffer.
            bool is_end_of_block = (i == p_data.current_idx) || 
                                   (i < FRAME_SEGMENTS - 1 && p_data.segments[i + 1].state != info.state) ||
                                   (i < FRAME_SEGMENTS - 1 && p_data.segments[i + 1].state == FS_NONE);

            if (info.duration > 1 && is_end_of_block) {
                std::string num_str = std::to_string(info.duration);
                float offset_x = (SEG_WIDTH / 2.0f); 
                DrawTextD3D(num_str.c_str(), (int)(x + offset_x), (int)(y_offset + 5.0f), D3DCOLOR_ARGB(255, 255, 255, 255), true);
            }
        }
    };

    DrawPlayerFrames(p1_frames, start_y_p1, true);
    DrawPlayerFrames(p2_frames, start_y_p2, false);
}
