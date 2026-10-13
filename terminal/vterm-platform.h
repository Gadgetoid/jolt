void jolt_vterm_exit(int code);
void jolt_vterm_abort(void);

#define exit jolt_vterm_exit
#define abort jolt_vterm_abort