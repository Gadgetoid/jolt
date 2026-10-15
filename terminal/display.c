#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <shellapi.h>
#include <vterm.h>
#include "display.h"
#include "scrollback.h"
#include "session.h"
#include "kernel.h"
#include "sound.h"
#include "tty.h"
#include "fonts.h"
#include "bitmapfont.h"

#define ORIGIN_X 2
#define ORIGIN_Y 0
#define COLUMN_LIMIT 255
#define MINIMUM_COLUMNS 10
#define MINIMUM_ROWS 2
#define RUN_LIMIT 256
#define CURSOR_HEIGHT 2
#define UNDERLINE_HEIGHT 1
#define JOLT_OSC_COMMAND 7770
#define OSC_CAPACITY (JOLT_GUEST_PATH + 16)
#define FONT_FACE L"Courier New"
#define NOTICE_CAPACITY 128
#define TASKBAR_CLASS L"HHTaskBar"
#define WINDOWED_STYLE (WS_VISIBLE | WS_CAPTION | WS_SYSMENU | WS_VSCROLL)
#define FULLSCREEN_STYLE (WS_VISIBLE | WS_POPUP)
#define CLOSING_STYLE (WS_VISIBLE | WS_CAPTION | WS_SYSMENU)

struct display_state {
    HWND window;
    VTerm *terminal;
    VTermScreen *screen;
    HFONT font, bold_font;
    WCHAR family[LF_FACESIZE];
    int face_missing;
    struct bitmap_font bitmap;
    int writing;
    char notice[NOTICE_CAPACITY];
    DWORD notice_length;
    int cell_width, cell_height;
    int cursor_visible;
    int scroll;
    int shown_count, shown_rows, shown_scroll;
    struct display_settings settings;
    const struct theme *theme;
    HBRUSH background_brush, foreground_brush;
    HBITMAP splash;
    int splash_loaded;
    char osc_text[OSC_CAPACITY];
    DWORD osc_length;
    display_command_handler command_handler;
};

static struct display_state view;

static struct jolt_tty *console(void) {
    return &jolt.kernel->ttys[JOLT_CONSOLE];
}

static RECT cell_bounds(int start_row, int end_row, int start_column, int end_column) {
    RECT bounds = {
        ORIGIN_X + start_column * view.cell_width, ORIGIN_Y + start_row * view.cell_height,
        ORIGIN_X + end_column * view.cell_width, ORIGIN_Y + end_row * view.cell_height
    };
    return bounds;
}

static void invalidate_cell(int row, int column) {
    RECT bounds = cell_bounds(row, row + 1, column, column + 1);
    InvalidateRect(view.window, &bounds, FALSE);
}

static void invalidate_all(void) {
    InvalidateRect(view.window, NULL, FALSE);
}

static COLORREF colour_of(const VTermColor *colour) {
    VTermColor converted = *colour;
    vterm_screen_convert_color_to_rgb(view.screen, &converted);
    return RGB(converted.rgb.red, converted.rgb.green, converted.rgb.blue);
}

static void style_cell(const VTermScreenCell *cell, struct styled_cell *styled) {
    uint32_t character = cell->chars[0];
    styled->character = L' ';
    if (character > 0 && character <= 0xffff) styled->character = (WCHAR)character;
    else if (character > 0xffff && character != (uint32_t)-1) styled->character = L'?';
    styled->style = 0;
    if (cell->attrs.bold) styled->style |= STYLE_BOLD;
    if (cell->attrs.underline) styled->style |= STYLE_UNDERLINE;
    if (cell->attrs.reverse) styled->style |= STYLE_REVERSE;
    if (VTERM_COLOR_IS_DEFAULT_FG(&cell->fg)) styled->style |= STYLE_DEFAULT_FOREGROUND;
    if (VTERM_COLOR_IS_DEFAULT_BG(&cell->bg)) styled->style |= STYLE_DEFAULT_BACKGROUND;
    styled->foreground = colour_of(&cell->fg);
    styled->background = colour_of(&cell->bg);
}

static void unstyle_cell(const struct styled_cell *styled, VTermScreenCell *cell) {
    memset(cell, 0, sizeof *cell);
    cell->chars[0] = styled->character;
    cell->width = 1;
    cell->attrs.bold = (styled->style & STYLE_BOLD) != 0;
    cell->attrs.underline = (styled->style & STYLE_UNDERLINE) != 0;
    cell->attrs.reverse = (styled->style & STYLE_REVERSE) != 0;
    vterm_color_rgb(&cell->fg, GetRValue(styled->foreground), GetGValue(styled->foreground), GetBValue(styled->foreground));
    vterm_color_rgb(&cell->bg, GetRValue(styled->background), GetGValue(styled->background), GetBValue(styled->background));
    if (styled->style & STYLE_DEFAULT_FOREGROUND) cell->fg.type |= VTERM_COLOR_DEFAULT_FG;
    if (styled->style & STYLE_DEFAULT_BACKGROUND) cell->bg.type |= VTERM_COLOR_DEFAULT_BG;
}

static void view_cell(int row, int column, struct styled_cell *styled) {
    int line = scrollback_count() - view.scroll + row;
    if (line < scrollback_count()) {
        scrollback_cell(line, column, styled);
        return;
    }
    VTermScreenCell cell;
    VTermPos position = { line - scrollback_count(), column };
    if (vterm_screen_get_cell(view.screen, position, &cell)) {
        style_cell(&cell, styled);
        return;
    }
    memset(styled, 0, sizeof *styled);
    styled->character = L' ';
    styled->style = STYLE_DEFAULT_FOREGROUND | STYLE_DEFAULT_BACKGROUND;
}

static void cell_colours(const struct styled_cell *cell, COLORREF *foreground, COLORREF *background) {
    *foreground = cell->style & STYLE_DEFAULT_FOREGROUND ? view.theme->foreground : cell->foreground;
    *background = cell->style & STYLE_DEFAULT_BACKGROUND ? view.theme->background : cell->background;
    if (cell->style & STYLE_REVERSE) {
        COLORREF swap = *foreground;
        *foreground = *background;
        *background = swap;
    }
}

static int same_style(const struct styled_cell *first, const struct styled_cell *second) {
    COLORREF first_foreground, first_background, second_foreground, second_background;
    cell_colours(first, &first_foreground, &first_background);
    cell_colours(second, &second_foreground, &second_background);
    return (first->style & (STYLE_BOLD | STYLE_UNDERLINE)) == (second->style & (STYLE_BOLD | STYLE_UNDERLINE)) &&
        first_foreground == second_foreground && first_background == second_background;
}

static void draw_bitmap_run(HDC dc, int row, int start, const WCHAR *text, int length, int bold) {
    for (int offset = 0; offset < length; offset++) {
        RECT cell = cell_bounds(row, row + 1, start + offset, start + offset + 1);
        int index = bitmap_font_index(&view.bitmap, text[offset]);
        if (index >= 0) bitmap_font_draw(dc, &view.bitmap, bold, index, cell.left, cell.top);
        else ExtTextOutW(dc, cell.left, cell.top, ETO_OPAQUE | ETO_CLIPPED, &cell, &text[offset], 1, NULL);
    }
}

static void draw_run(HDC dc, int row, int start, const WCHAR *text, int length, const struct styled_cell *style,
    const INT *advances) {
    COLORREF foreground, background;
    cell_colours(style, &foreground, &background);
    int bold = (style->style & STYLE_BOLD) != 0;
    SelectObject(dc, bold ? view.bold_font : view.font);
    SetTextColor(dc, foreground);
    SetBkColor(dc, background);
    RECT bounds = cell_bounds(row, row + 1, start, start + length);
    if (view.bitmap.count) draw_bitmap_run(dc, row, start, text, length, bold);
    else ExtTextOutW(dc, bounds.left, bounds.top, ETO_OPAQUE | ETO_CLIPPED, &bounds, text, (UINT)length, advances);
    if (!(style->style & STYLE_UNDERLINE)) return;
    HBRUSH brush = CreateSolidBrush(foreground);
    bounds.top = bounds.bottom - UNDERLINE_HEIGHT;
    FillRect(dc, &bounds, brush);
    DeleteObject(brush);
}

static void paint_row(HDC dc, int row, int first_column, int last_column, const INT *advances) {
    WCHAR text[RUN_LIMIT];
    struct styled_cell run_style, cell;
    int run_start = first_column, run_length = 0;
    for (int column = first_column; column < last_column; column++) {
        view_cell(row, column, &cell);
        if (run_length && (!same_style(&cell, &run_style) || run_length == RUN_LIMIT)) {
            draw_run(dc, row, run_start, text, run_length, &run_style, advances);
            run_length = 0;
        }
        if (!run_length) {
            run_start = column;
            run_style = cell;
        }
        text[run_length++] = cell.character;
    }
    if (run_length) draw_run(dc, row, run_start, text, run_length, &run_style, advances);
}

static void paint_cursor(HDC dc) {
    if (session.finished || !view.cursor_visible || view.scroll) return;
    VTermPos cursor;
    vterm_state_get_cursorpos(vterm_obtain_state(view.terminal), &cursor);
    RECT bounds = cell_bounds(cursor.row, cursor.row + 1, cursor.col, cursor.col + 1);
    bounds.top = bounds.bottom - CURSOR_HEIGHT;
    FillRect(dc, &bounds, view.foreground_brush);
}

static void paint_cells(HDC dc, const RECT *area) {
    int first_row = (area->top - ORIGIN_Y) / view.cell_height;
    int last_row = (area->bottom - ORIGIN_Y + view.cell_height - 1) / view.cell_height;
    int first_column = (area->left - ORIGIN_X) / view.cell_width;
    int last_column = (area->right - ORIGIN_X + view.cell_width - 1) / view.cell_width;
    if (first_row < 0) first_row = 0;
    if (first_column < 0) first_column = 0;
    if (last_row > console()->rows) last_row = console()->rows;
    if (last_column > console()->columns) last_column = console()->columns;
    INT advances[RUN_LIMIT];
    for (int index = 0; index < RUN_LIMIT; index++) advances[index] = view.cell_width;
    SetBkMode(dc, OPAQUE);
    for (int row = first_row; row < last_row; row++) paint_row(dc, row, first_column, last_column, advances);
    paint_cursor(dc);
}

static void paint_splash(HDC dc, const RECT *area) {
    RECT client;
    GetClientRect(view.window, &client);
    FillRect(dc, area, GetStockObject(BLACK_BRUSH));
    BITMAP size;
    HDC source = CreateCompatibleDC(dc);
    if (!source) return;
    if (GetObjectW(view.splash, sizeof size, &size)) {
        HGDIOBJ previous = SelectObject(source, view.splash);
        BitBlt(dc, (client.right - size.bmWidth) / 2, (client.bottom - size.bmHeight) / 2,
            size.bmWidth, size.bmHeight, source, 0, 0, SRCCOPY);
        SelectObject(source, previous);
    }
    DeleteDC(source);
}

void display_paint(void) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(view.window, &paint);
    if (view.splash) {
        paint_splash(dc, &paint.rcPaint);
    } else {
        paint_cells(dc, &paint.rcPaint);
        RECT grid = cell_bounds(0, console()->rows, 0, console()->columns);
        ExcludeClipRect(dc, grid.left, grid.top, grid.right, grid.bottom);
        FillRect(dc, &paint.rcPaint, view.background_brush);
    }
    EndPaint(view.window, &paint);
}

static void update_scrollbar(void) {
    if (view.settings.fullscreen) return;
    int count = scrollback_count(), rows = console()->rows;
    if (count == view.shown_count && rows == view.shown_rows && view.scroll == view.shown_scroll) return;
    view.shown_count = count;
    view.shown_rows = rows;
    view.shown_scroll = view.scroll;
    SCROLLINFO information;
    memset(&information, 0, sizeof information);
    information.cbSize = sizeof information;
    information.fMask = SIF_PAGE | SIF_POS | SIF_RANGE | SIF_DISABLENOSCROLL;
    information.nMax = count + rows - 1;
    information.nPage = (UINT)rows;
    information.nPos = count - view.scroll;
    SetScrollInfo(view.window, SB_VERT, &information, TRUE);
}

void display_scroll_to(int lines_back) {
    if (lines_back < 0) lines_back = 0;
    if (lines_back > scrollback_count()) lines_back = scrollback_count();
    if (lines_back == view.scroll) return;
    view.scroll = lines_back;
    invalidate_all();
    update_scrollbar();
}

void display_scroll_by(int lines) {
    display_scroll_to(view.scroll + lines);
}

int display_page_lines(void) {
    return console()->rows > 1 ? console()->rows - 1 : 1;
}

void display_scrollbar(WPARAM request) {
    int top = scrollback_count() - view.scroll, page = display_page_lines();
    switch (LOWORD(request)) {
    case SB_LINEUP: top--; break;
    case SB_LINEDOWN: top++; break;
    case SB_PAGEUP: top -= page; break;
    case SB_PAGEDOWN: top += page; break;
    case SB_THUMBTRACK: case SB_THUMBPOSITION: top = (short)HIWORD(request); break;
    case SB_TOP: top = 0; break;
    case SB_BOTTOM: top = scrollback_count(); break;
    }
    display_scroll_to(scrollback_count() - top);
}

static int on_damage(VTermRect rect, void *user) {
    (void)user;
    if (view.scroll) {
        invalidate_all();
        return 1;
    }
    RECT bounds = cell_bounds(rect.start_row, rect.end_row, rect.start_col, rect.end_col);
    InvalidateRect(view.window, &bounds, FALSE);
    return 1;
}

static int on_cursor(VTermPos position, VTermPos previous, int visible, void *user) {
    (void)user;
    invalidate_cell(previous.row, previous.col);
    invalidate_cell(position.row, position.col);
    view.cursor_visible = visible;
    return 1;
}

static int on_property(VTermProp property, VTermValue *value, void *user) {
    (void)user;
    if (property != VTERM_PROP_CURSORVISIBLE) return 0;
    view.cursor_visible = value->boolean;
    VTermPos cursor;
    vterm_state_get_cursorpos(vterm_obtain_state(view.terminal), &cursor);
    invalidate_cell(cursor.row, cursor.col);
    return 1;
}

static int on_push_line(int columns, const VTermScreenCell *cells, void *user) {
    (void)user;
    struct styled_cell styled[COLUMN_LIMIT];
    if (columns > COLUMN_LIMIT) columns = COLUMN_LIMIT;
    for (int column = 0; column < columns; column++) style_cell(&cells[column], &styled[column]);
    scrollback_push(columns, styled);
    if (view.scroll && view.scroll < scrollback_count()) view.scroll++;
    return 1;
}

static int on_pop_line(int columns, VTermScreenCell *cells, void *user) {
    (void)user;
    struct styled_cell styled[COLUMN_LIMIT];
    if (columns > COLUMN_LIMIT) columns = COLUMN_LIMIT;
    if (!scrollback_pop(columns, styled)) return 0;
    for (int column = 0; column < columns; column++) unstyle_cell(&styled[column], &cells[column]);
    if (view.scroll > scrollback_count()) view.scroll = scrollback_count();
    return 1;
}

static int on_osc(int command, VTermStringFragment fragment, void *user) {
    (void)user;
    if (command != JOLT_OSC_COMMAND) return 0;
    if (fragment.initial) view.osc_length = 0;
    for (size_t index = 0; index < fragment.len && view.osc_length < OSC_CAPACITY - 1; index++)
        view.osc_text[view.osc_length++] = fragment.str[index];
    view.osc_text[view.osc_length] = 0;
    if (fragment.final && view.command_handler) view.command_handler(view.osc_text);
    return 1;
}

static int on_bell(void *user) {
    (void)user;
    sound_bell();
    return 1;
}

static void on_terminal_output(const char *bytes, size_t length, void *user) {
    (void)user;
    tty_type(bytes, (DWORD)length);
}

static const VTermScreenCallbacks screen_callbacks = {
    .damage = on_damage,
    .movecursor = on_cursor,
    .settermprop = on_property,
    .bell = on_bell,
    .sb_pushline = on_push_line,
    .sb_popline = on_pop_line,
};

static const VTermStateFallbacks state_fallbacks = {
    .osc = on_osc,
};

void display_output(const char *bytes, DWORD count) {
    session_record_output(bytes, count);
    if (view.splash) {
        DeleteObject(view.splash);
        view.splash = NULL;
        invalidate_all();
    }
    view.writing = 1;
    vterm_input_write(view.terminal, bytes, count);
    view.writing = 0;
    if (view.notice_length) {
        DWORD length = view.notice_length;
        view.notice_length = 0;
        vterm_input_write(view.terminal, view.notice, length);
    }
    vterm_screen_flush_damage(view.screen);
    update_scrollbar();
}

void display_notice(const char *text) {
    DWORD length = (DWORD)strlen(text);
    if (!view.writing) {
        display_output(text, length);
        return;
    }
    if (length > sizeof view.notice - view.notice_length) length = sizeof view.notice - view.notice_length;
    memcpy(view.notice + view.notice_length, text, length);
    view.notice_length += length;
}

void display_drain_tty(void) {
    struct jolt_tty *tty = console();
    char buffer[JOLT_TTY_SIZE];
    DWORD consumed = (DWORD)tty->output_read;
    DWORD count = (DWORD)tty->output_written - consumed;
    if (!count) return;
    if (count > sizeof buffer) count = sizeof buffer;
    for (DWORD index = 0; index < count; index++) buffer[index] = tty->output[(consumed + index) % JOLT_TTY_SIZE];
    InterlockedExchange(&tty->output_read, (LONG)(consumed + count));
    display_output(buffer, count);
}

static HFONT create_font(int height, int weight, const WCHAR *family) {
    LOGFONTW font;
    memset(&font, 0, sizeof font);
    font.lfHeight = height;
    font.lfWeight = weight;
    font.lfCharSet = DEFAULT_CHARSET;
    font.lfPitchAndFamily = FIXED_PITCH;
    wcscpy(font.lfFaceName, family);
    return CreateFontIndirectW(&font);
}

static int measure_font(HFONT font, int require_fixed, TEXTMETRICW *metrics) {
    HDC dc = GetDC(view.window);
    HGDIOBJ previous = SelectObject(dc, font);
    int measured = GetTextMetricsW(dc, metrics) && metrics->tmAveCharWidth > 0 && metrics->tmHeight > 0;
    SelectObject(dc, previous);
    ReleaseDC(view.window, dc);
    return measured && (!require_fixed || !(metrics->tmPitchAndFamily & TMPF_FIXED_PITCH));
}

static int create_fonts(const WCHAR *family, int require_fixed, int height) {
    HFONT regular = create_font(height, FW_NORMAL, family);
    HFONT bold = create_font(height, FW_BOLD, family);
    TEXTMETRICW metrics;
    if (!regular || !bold || !measure_font(regular, require_fixed, &metrics)) {
        if (regular) DeleteObject(regular);
        if (bold) DeleteObject(bold);
        return 0;
    }
    if (view.font) DeleteObject(view.font);
    if (view.bold_font) DeleteObject(view.bold_font);
    view.font = regular;
    view.bold_font = bold;
    view.cell_width = metrics.tmAveCharWidth;
    view.cell_height = metrics.tmHeight;
    wcscpy(view.family, family);
    return 1;
}

static int resolve_face(WCHAR *family) {
    HDC dc = GetDC(view.window);
    int found = fonts_resolve(dc, view.settings.face, family);
    ReleaseDC(view.window, dc);
    return found;
}

static int load_bitmap_font(void) {
    int height = bitmap_font_pick(view.settings.face, view.settings.font_height);
    struct bitmap_font font;
    if (!height || !bitmap_font_load(view.settings.face, height, &font)) return 0;
    if (!create_fonts(FONT_FACE, 0, font.height)) {
        bitmap_font_free(&font);
        return 0;
    }
    bitmap_font_free(&view.bitmap);
    view.bitmap = font;
    view.cell_width = font.width;
    view.cell_height = font.height;
    size_t index = 0;
    for (; view.settings.face[index] && index + 1 < LF_FACESIZE; index++) view.family[index] = (WCHAR)(unsigned char)view.settings.face[index];
    view.family[index] = 0;
    return 1;
}

static void load_fonts(void) {
    WCHAR family[LF_FACESIZE];
    view.face_missing = 0;
    if (view.settings.face[0]) {
        if (load_bitmap_font()) return;
        bitmap_font_free(&view.bitmap);
        if (resolve_face(family) && create_fonts(family, 1, view.settings.font_height)) return;
        view.face_missing = 1;
    }
    bitmap_font_free(&view.bitmap);
    create_fonts(FONT_FACE, 0, view.settings.font_height);
}

static void vterm_colour(VTermColor *colour, COLORREF value) {
    vterm_color_rgb(colour, GetRValue(value), GetGValue(value), GetBValue(value));
}

static void apply_theme(void) {
    view.theme = theme_lookup(view.settings.theme, view.settings.dark);
    if (view.foreground_brush) DeleteObject(view.foreground_brush);
    if (view.background_brush) DeleteObject(view.background_brush);
    view.foreground_brush = CreateSolidBrush(view.theme->foreground);
    view.background_brush = CreateSolidBrush(view.theme->background);
    if (!view.terminal) return;
    VTermState *state = vterm_obtain_state(view.terminal);
    vterm_state_set_bold_highbright(state, view.settings.theme == THEME_CLASSIC);
    VTermColor foreground, background;
    vterm_colour(&foreground, view.theme->foreground);
    vterm_colour(&background, view.theme->background);
    vterm_state_set_default_colors(state, &foreground, &background);
    for (int index = 0; index < THEME_ANSI_COLOURS; index++) {
        VTermColor colour;
        vterm_colour(&colour, view.theme->ansi[index]);
        vterm_state_set_palette_color(state, index, &colour);
    }
}

static void grid_size(int *rows, int *columns) {
    RECT client;
    GetClientRect(view.window, &client);
    *columns = (client.right - ORIGIN_X) / view.cell_width;
    *rows = (client.bottom - ORIGIN_Y) / view.cell_height;
    if (*columns > COLUMN_LIMIT) *columns = COLUMN_LIMIT;
}

void display_resize(void) {
    if (!view.terminal) return;
    int rows, columns;
    grid_size(&rows, &columns);
    if (columns < MINIMUM_COLUMNS || rows < MINIMUM_ROWS) return;
    if (columns == console()->columns && rows == console()->rows) return;
    vterm_set_size(view.terminal, rows, columns);
    vterm_screen_flush_damage(view.screen);
    jolt_tty_resize(JOLT_CONSOLE, (WORD)rows, (WORD)columns);
    if (view.scroll > scrollback_count()) view.scroll = scrollback_count();
    update_scrollbar();
    invalidate_all();
}

static void work_area(RECT *area) {
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, area, 0))
        SetRect(area, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}

void display_restore_taskbar(void) {
    HWND taskbar = FindWindowW(TASKBAR_CLASS, NULL);
    if (taskbar) ShowWindow(taskbar, SW_SHOW);
}

static void apply_layout(void) {
    RECT area;
    HWND taskbar = FindWindowW(TASKBAR_CLASS, NULL);
    if (view.settings.fullscreen) {
        SetRect(&area, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
        if (taskbar) ShowWindow(taskbar, SW_HIDE);
    } else {
        if (taskbar) ShowWindow(taskbar, SW_SHOW);
        work_area(&area);
    }
    SetWindowLongW(view.window, GWL_STYLE, view.settings.fullscreen ? FULLSCREEN_STYLE : WINDOWED_STYLE);
    view.shown_count = -1;
    SetWindowPos(view.window, view.settings.fullscreen ? HWND_TOPMOST : HWND_NOTOPMOST, area.left, area.top,
        area.right - area.left, area.bottom - area.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    display_resize();
    invalidate_all();
}

void display_apply(const struct display_settings *settings) {
    int font_changed = settings->font_height != view.settings.font_height || strcmp(settings->face, view.settings.face);
    view.settings = *settings;
    if (font_changed) load_fonts();
    apply_theme();
    apply_layout();
}

int display_create(HWND window, const struct display_settings *settings, display_command_handler handler) {
    memset(&view, 0, sizeof view);
    view.window = window;
    view.settings = *settings;
    view.command_handler = handler;
    view.cursor_visible = 1;
    view.shown_count = view.shown_rows = view.shown_scroll = -1;
    view.cell_width = 8;
    view.cell_height = 16;
    if (!scrollback_init()) return 0;
    load_fonts();
    int rows, columns;
    grid_size(&rows, &columns);
    console()->rows = (WORD)rows;
    console()->columns = (WORD)columns;
    view.terminal = vterm_new(rows, columns);
    if (!view.terminal) return 0;
    vterm_set_utf8(view.terminal, 1);
    vterm_output_set_callback(view.terminal, on_terminal_output, NULL);
    view.screen = vterm_obtain_screen(view.terminal);
    vterm_screen_set_callbacks(view.screen, &screen_callbacks, NULL);
    vterm_screen_set_unrecognised_fallbacks(view.screen, &state_fallbacks, NULL);
    vterm_screen_set_damage_merge(view.screen, VTERM_DAMAGE_SCREEN);
    vterm_screen_enable_altscreen(view.screen, 1);
    apply_theme();
    vterm_screen_reset(view.screen, 1);
    if (view.settings.fullscreen) apply_layout();
    return 1;
}

void display_destroy(void) {
    if (view.font) DeleteObject(view.font);
    if (view.bold_font) DeleteObject(view.bold_font);
    if (view.splash) DeleteObject(view.splash);
    if (view.foreground_brush) DeleteObject(view.foreground_brush);
    if (view.background_brush) DeleteObject(view.background_brush);
    if (view.terminal) vterm_free(view.terminal);
    bitmap_font_free(&view.bitmap);
    fonts_release();
    scrollback_free();
    memset(&view, 0, sizeof view);
}

void display_show_splash(const WCHAR *path) {
    view.splash = SHLoadDIBitmap(path);
    view.splash_loaded = view.splash != NULL;
    if (!view.splash) return;
    invalidate_all();
    UpdateWindow(view.window);
}

int display_splash_loaded(void) {
    return view.splash_loaded;
}

DWORD display_screen_text(char *buffer, DWORD capacity) {
    DWORD used = 0;
    if (!view.screen) return 0;
    for (int row = 0; row < console()->rows; row++) {
        char text[COLUMN_LIMIT];
        int end = 0;
        for (int column = 0; column < console()->columns && column < COLUMN_LIMIT; column++) {
            VTermScreenCell cell;
            VTermPos position = { row, column };
            char character = ' ';
            if (vterm_screen_get_cell(view.screen, position, &cell) && cell.chars[0] > ' ' && cell.chars[0] < 127)
                character = (char)cell.chars[0];
            text[column] = character;
            if (character != ' ') end = column + 1;
        }
        if (used + (DWORD)end + 2 >= capacity) break;
        memcpy(buffer + used, text, (size_t)end);
        used += (DWORD)end;
        buffer[used++] = '\r';
        buffer[used++] = '\n';
    }
    return used;
}

DWORD display_describe(char *buffer, DWORD capacity) {
    char family[LF_FACESIZE];
    size_t index = 0;
    for (; view.family[index] && index + 1 < sizeof family; index++) family[index] = (char)view.family[index];
    family[index] = 0;
    int length = snprintf(buffer, capacity,
        "theme=%s dark=%d fullscreen=%d font=%d face=%s foreground=%06lx background=%06lx\r\n",
        theme_label(view.settings.theme), view.settings.dark, view.settings.fullscreen, view.settings.font_height, family,
        (unsigned long)view.theme->foreground, (unsigned long)view.theme->background);
    return length > 0 ? (DWORD)length : 0;
}

int display_face_missing(void) {
    return view.face_missing;
}

void display_close(void) {
    display_restore_taskbar();
    SetWindowLongW(view.window, GWL_STYLE, CLOSING_STYLE);
    SetWindowPos(view.window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
    DestroyWindow(view.window);
}

void display_key(VTermKey key, VTermModifier modifiers) {
    if (view.terminal) vterm_keyboard_key(view.terminal, key, modifiers);
}
