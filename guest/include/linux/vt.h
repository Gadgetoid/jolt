#pragma once

#define VT_OPENQRY 0x5600
#define VT_GETMODE 0x5601
#define VT_SETMODE 0x5602
#define VT_GETSTATE 0x5603
#define VT_ACTIVATE 0x5606
#define VT_WAITACTIVE 0x5607

struct vt_stat {
    unsigned short v_active;
    unsigned short v_signal;
    unsigned short v_state;
};

struct vt_mode {
    char mode;
    char waitv;
    short relsig;
    short acqsig;
    short frsig;
};
