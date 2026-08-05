
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <commctrl.h>

#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "driver.h"
#include "gui_main.h"
#include "ipc.h"
#include "kernel.h"
#include "mm.h"
#include "scheduler.h"


#define IDM_STEP            1001
#define IDM_RUN             1002
#define IDM_RUN_DEMO        1003
#define IDM_RESET           1004
#define IDM_DUMP            1005
#define IDM_CMB_POLICY      1006
#define IDM_CHK_PREEMPT     1007
#define IDM_EDT_RUNTICKS    1008
#define IDM_EDT_SPAWN_NAME  1009
#define IDM_CMB_SPAWN_PRI   1010
#define IDM_SPAWN           1011
#define IDM_EDT_MOVE        1012
#define IDM_SEND_MOVE       1013
#define IDM_EDT_STATE       1014
#define IDM_SEND_STATE      1015
#define IDM_DRAW_OFFER      1016
#define IDM_DRAW_ACCEPT     1017
#define IDM_RESIGN          1018


static HWND g_hMain;
static HWND g_hCmbPolicy, g_hChkPreempt, g_hEdtRunTicks;
static HWND g_hEdtSpawnName, g_hCmbSpawnPri;
static HWND g_hEdtMove, g_hEdtState;
static HWND g_hLog;                 
static HWND g_hPanel;               


#define WM_APP_LOGLINE  (WM_APP + 1)

#define WM_APP_REFRESH  (WM_APP + 2)





static HANDLE g_pipeRead = NULL;

static void redirect_stdout_to_pipe(void) {
    HANDLE writeEnd = NULL;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&g_pipeRead, &writeEnd, &sa, 0)) {
        return;
    }
    
    SetHandleInformation(g_pipeRead, HANDLE_FLAG_INHERIT, 0);

    int fd = _open_osfhandle((intptr_t)writeEnd, _O_TEXT);
    if (fd < 0) {
        return;
    }
    _dup2(fd, 1);                       
    _dup2(fd, 2);                       
    setvbuf(stdout, NULL, _IONBF, 0);
    _close(fd);
}

static DWORD WINAPI reader_thread(LPVOID arg) {
    (void)arg;
    char buf[1024];
    DWORD avail;
    HANDLE r = g_pipeRead;

    for (;;) {
        
        if (!PeekNamedPipe(r, NULL, 0, NULL, &avail, NULL)) {
            break;                      
        }
        if (avail == 0) {
            Sleep(20);
            continue;
        }
        DWORD toRead = avail < sizeof(buf) - 1 ? avail : (DWORD)sizeof(buf) - 1;
        DWORD got = 0;
        if (!ReadFile(r, buf, toRead, &got, NULL) || got == 0) {
            break;
        }
        buf[got] = '\0';

        
        char* line = buf;
        char* nl;
        while ((nl = strchr(line, '\n')) != NULL) {
            *nl = '\0';
            size_t len = strlen(line);
            
            while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
                line[--len] = '\0';
            }
            char* copy = (char*)LocalAlloc(LMEM_FIXED, len + 2);
            if (copy) {
                memcpy(copy, line, len);
                copy[len] = '\n';
                copy[len + 1] = '\0';
                PostMessage(g_hMain, WM_APP_LOGLINE, 0, (LPARAM)copy);
            }
            line = nl + 1;
        }
        if (*line) {
            
            size_t len = strlen(line);
            char* copy = (char*)LocalAlloc(LMEM_FIXED, len + 2);
            if (copy) {
                memcpy(copy, line, len);
                copy[len] = '\n';
                copy[len + 1] = '\0';
                PostMessage(g_hMain, WM_APP_LOGLINE, 0, (LPARAM)copy);
            }
        }
    }
    return 0;
}





typedef struct {
    gui_action_t action;
    char text_a[80];                 
    char text_b[80];
    uint8_t priority;
    uint64_t ticks;
} request_t;

static CRITICAL_SECTION g_reqLock;
static CONDITION_VARIABLE g_reqCv;
static request_t g_pending;
static volatile LONG g_workerStop = 0;

static void exec_request(const request_t* req) {
    
    
    uint64_t chess_target = 1;

    switch (req->action) {
    case GUI_ACT_STEP:
        scheduler_run(1);
        break;
    case GUI_ACT_RUN:
        scheduler_run(req->ticks ? req->ticks : 8);
        break;
    case GUI_ACT_RUN_DEMO:
        kernel_run_demo();
        break;
    case GUI_ACT_RESET:
        kernel_reset();
        break;
    case GUI_ACT_DUMP:
        scheduler_dump_state();
        print_memory_stats();
        break;
    case GUI_ACT_SPAWN:
        kernel_spawn_process(req->text_a[0] ? req->text_a : "spawned", req->priority);
        break;
    case GUI_ACT_CHESS_MOVE:
        chess_send_move(chess_target, req->text_a[0] ? req->text_a : "e2e4", 0);
        ipc_poll(4);
        break;
    case GUI_ACT_CHESS_STATE:
        chess_send_state(chess_target, req->text_a[0] ? req->text_a : "startpos");
        ipc_poll(4);
        break;
    case GUI_ACT_CHESS_DRAW_OFFER:
        chess_send_draw_offer(chess_target);
        ipc_poll(4);
        break;
    case GUI_ACT_CHESS_DRAW_ACCEPT:
        chess_send_draw_accept(chess_target);
        ipc_poll(4);
        break;
    case GUI_ACT_CHESS_RESIGN:
        chess_send_resign(chess_target);
        ipc_poll(4);
        break;
    default:
        break;
    }
}

static DWORD WINAPI worker_thread(LPVOID arg) {
    (void)arg;
    for (;;) {
        request_t req;
        EnterCriticalSection(&g_reqLock);
        while (g_pending.action == GUI_ACT_NONE && !g_workerStop) {
            SleepConditionVariableCS(&g_reqCv, &g_reqLock, INFINITE);
        }
        if (g_workerStop) {
            LeaveCriticalSection(&g_reqLock);
            break;
        }
        req = g_pending;
        g_pending.action = GUI_ACT_NONE;
        LeaveCriticalSection(&g_reqLock);

        exec_request(&req);

        PostMessage(g_hMain, WM_APP_REFRESH, 0, 0);
    }
    return 0;
}


static void post_request(const request_t* req) {
    EnterCriticalSection(&g_reqLock);
    g_pending = *req;
    WakeConditionVariable(&g_reqCv);
    LeaveCriticalSection(&g_reqLock);
}





static void get_edit_text(HWND edit, char* out, size_t cap) {
    out[0] = '\0';
    GetWindowTextA(edit, out, (int)cap);
}

static int combo_index(HWND combo) {
    return (int)SendMessageA(combo, CB_GETCURSEL, 0, 0);
}

static uint8_t priority_from_index(int idx) {
    
    switch (idx) {
    case 0:  return SCHED_PRIORITY_LOW;
    case 2:  return SCHED_PRIORITY_HIGH;
    default: return SCHED_PRIORITY_NORMAL;
    }
}





static const char* state_name(uint8_t state) {
    switch (state) {
    case PROC_STATE_READY:    return "READY";
    case PROC_STATE_RUNNING:  return "RUNNING";
    case PROC_STATE_SLEEPING: return "SLEEPING";
    case PROC_STATE_ZOMBIE:   return "ZOMBIE";
    default:                  return "?";
    }
}

static const char* priority_name(uint8_t pri) {
    if (pri >= SCHED_PRIORITY_HIGH)   return "HIGH";
    if (pri >= SCHED_PRIORITY_NORMAL) return "NORMAL";
    if (pri >= SCHED_PRIORITY_LOW)    return "LOW";
    return "IDLE";
}

static const char* driver_type_name(uint8_t type) {
    switch (type) {
    case DRIVER_TYPE_CHAR:  return "char";
    case DRIVER_TYPE_BLOCK: return "block";
    case DRIVER_TYPE_NET:   return "net";
    case DRIVER_TYPE_INPUT: return "input";
    default:                return "?";
    }
}

static void draw_panel(void) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(g_hPanel, &ps);
    if (!hdc) {
        return;
    }

    RECT rc;
    GetClientRect(g_hPanel, &rc);
    
    HBRUSH bg = CreateSolidBrush(RGB(24, 24, 28));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);
    rc.left += 10; rc.top += 8; rc.right -= 10; rc.bottom -= 8;

    HFONT font = (HFONT)GetStockObject(ANSI_FIXED_FONT);
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, RGB(220, 220, 220));
    SetBkMode(hdc, TRANSPARENT);

    char line[256];
    int y = rc.top;
    int const lineH = 16;

    
    scheduler_stats_t* s = get_scheduler_stats();
    snprintf(line, sizeof(line), "SCHEDULER   ticks=%llu  switches=%llu  created=%llu  terminated=%llu  idle=%llu  work=%llu",
             (unsigned long long)s->scheduler_ticks,
             (unsigned long long)s->context_switches,
             (unsigned long long)s->processes_created,
             (unsigned long long)s->processes_terminated,
             (unsigned long long)s->idle_time,
             (unsigned long long)s->total_process_time);
    TextOutA(hdc, rc.left, y, line, (int)strlen(line)); y += lineH;

    process_t* cur = scheduler_get_current();
    snprintf(line, sizeof(line), "current: %s (pid=%llu)",
             cur ? cur->name : "none",
             cur ? (unsigned long long)cur->pid : 0ULL);
    TextOutA(hdc, rc.left, y, line, (int)strlen(line)); y += lineH + 4;

    
    TextOutA(hdc, rc.left, y, "READY QUEUE:", 12); y += lineH;
    process_t* head = scheduler_get_ready_head();
    if (!head) {
        TextOutA(hdc, rc.left + 12, y, "  (empty)", 9); y += lineH;
    } else {
        process_t* p = head;
        int count = 0;
        do {
            snprintf(line, sizeof(line), "  %-12s pid=%-4llu pri=%-6s state=%s cpu=%llu",
                     p->name,
                     (unsigned long long)p->pid,
                     priority_name(p->priority),
                     state_name(p->state),
                     (unsigned long long)p->cpu_time);
            TextOutA(hdc, rc.left + 12, y, line, (int)strlen(line)); y += lineH;
            p = process_get_next(p);
            if (++count > 32) break;     
        } while (p && p != head);
    }
    y += 4;

    
    mem_stats_t m = get_memory_stats();
    unsigned usagePct = (unsigned)((KERNEL_HEAP_SIZE > 0)
        ? (m.current_usage * 100 / KERNEL_HEAP_SIZE) : 0);
    snprintf(line, sizeof(line), "MEMORY      current=%llu (%u%%)  peak=%llu  allocs=%llu  frees=%llu  failed=%llu",
             (unsigned long long)m.current_usage, usagePct,
             (unsigned long long)m.peak_usage,
             (unsigned long long)m.allocation_count,
             (unsigned long long)m.free_count,
             (unsigned long long)m.failed_allocations);
    TextOutA(hdc, rc.left, y, line, (int)strlen(line)); y += lineH;

    
    RECT bar = { rc.left, y + 2, rc.left + 200, y + 12 };
    FrameRect(hdc, &bar, (HBRUSH)GetStockObject(DKGRAY_BRUSH));
    RECT fill = bar;
    fill.right = bar.left + (long)(200 * usagePct / 100);
    HBRUSH fillBrush = CreateSolidBrush(RGB(80, 200, 120));
    FillRect(hdc, &fill, fillBrush);
    DeleteObject(fillBrush);
    y += lineH + 6;

    
    TextOutA(hdc, rc.left, y, "DRIVERS:", 8); y += lineH;
    int shown = 0;
    for (driver_t* d = driver_get_list_head(); d; d = d->next) {
        snprintf(line, sizeof(line), "  %s (%s)", d->name, driver_type_name(d->type));
        TextOutA(hdc, rc.left + 12, y, line, (int)strlen(line)); y += lineH;
        if (++shown > 8) break;
    }
    if (shown == 0) {
        TextOutA(hdc, rc.left + 12, y, "  (none)", 8); y += lineH;
    }

    snprintf(line, sizeof(line), "IPC queue: %llu pending",
             (unsigned long long)ipc_pending_count());
    TextOutA(hdc, rc.left, y, line, (int)strlen(line)); y += lineH;

    SelectObject(hdc, oldFont);
    EndPaint(g_hPanel, &ps);
}





static void append_log_line(const char* text) {
    int len = GetWindowTextLengthA(g_hLog);
    SendMessageA(g_hLog, EM_SETSEL, len, len);
    SendMessageA(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)text);
    
    len = GetWindowTextLengthA(g_hLog);
    SendMessageA(g_hLog, EM_SETSEL, len, len);
    SendMessageA(g_hLog, EM_SCROLLCARET, 0, 0);
}





static HWND make_ctl(const char* cls, const char* text, DWORD style,
                     DWORD exStyle, int x, int y, int w, int ht,
                     int id, HFONT font) {
    HWND hwndCtl = CreateWindowExA(exStyle, cls, text, style, x, y, w, ht,
                                   g_hMain, (HMENU)(INT_PTR)id, NULL, NULL);
    if (hwndCtl && font) {
        SendMessageA(hwndCtl, WM_SETFONT, (WPARAM)font, FALSE);
    }
    return hwndCtl;
}





static void build_ui(HWND hwnd, HINSTANCE hInst) {
    (void)hwnd;
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    
    int y = 8;
    make_ctl("BUTTON", "Step 1", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
             0, 8, y, 80, 26, IDM_STEP, font);
    make_ctl("BUTTON", "Run", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
             0, 92, y, 60, 26, IDM_RUN, font);
    g_hEdtRunTicks = make_ctl("EDIT", "8", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                              0, 156, y + 3, 40, 26, IDM_EDT_RUNTICKS, font);
    make_ctl("BUTTON", "Run Demo", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
             0, 202, y, 90, 26, IDM_RUN_DEMO, font);
    make_ctl("BUTTON", "Reset", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
             0, 298, y, 70, 26, IDM_RESET, font);
    make_ctl("BUTTON", "Dump", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
             0, 372, y, 70, 26, IDM_DUMP, font);

    
    HWND lbl = make_ctl("STATIC", "Policy:", WS_CHILD | WS_VISIBLE | SS_LEFT,
                        0, 450, y + 6, 44, 20, 0, font);
    (void)lbl;
    g_hCmbPolicy = make_ctl("COMBOBOX", NULL,
                            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                            0, 496, y, 130, 200, IDM_CMB_POLICY, font);
    SendMessageA(g_hCmbPolicy, CB_ADDSTRING, 0, (LPARAM)"Round-robin");
    SendMessageA(g_hCmbPolicy, CB_ADDSTRING, 0, (LPARAM)"Priority");
    SendMessageA(g_hCmbPolicy, CB_SETCURSEL, 0, 0);

    g_hChkPreempt = make_ctl("BUTTON", "Preemptive",
                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                             0, 636, y + 4, 110, 20, IDM_CHK_PREEMPT, font);
    SendMessageA(g_hChkPreempt, BM_SETCHECK, BST_CHECKED, 0);

    
    y = 44;
    make_ctl("STATIC", "Spawn:", WS_CHILD | WS_VISIBLE | SS_LEFT,
             0, 8, y + 6, 44, 20, 0, font);
    g_hEdtSpawnName = make_ctl("EDIT", "proc", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                               0, 56, y + 2, 120, 24, IDM_EDT_SPAWN_NAME, font);
    g_hCmbSpawnPri = make_ctl("COMBOBOX", NULL,
                              WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                              0, 182, y, 100, 200, IDM_CMB_SPAWN_PRI, font);
    SendMessageA(g_hCmbSpawnPri, CB_ADDSTRING, 0, (LPARAM)"Low");
    SendMessageA(g_hCmbSpawnPri, CB_ADDSTRING, 0, (LPARAM)"Normal");
    SendMessageA(g_hCmbSpawnPri, CB_ADDSTRING, 0, (LPARAM)"High");
    SendMessageA(g_hCmbSpawnPri, CB_SETCURSEL, 1, 0);
    make_ctl("BUTTON", "Spawn", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
             0, 288, y, 70, 24, IDM_SPAWN, font);

    
    y = 78;
    make_ctl("STATIC", "Chess move:", WS_CHILD | WS_VISIBLE | SS_LEFT,
             0, 8, y + 6, 70, 20, 0, font);
    g_hEdtMove = make_ctl("EDIT", "e2e4", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                          0, 82, y + 2, 80, 24, IDM_EDT_MOVE, font);
    make_ctl("BUTTON", "Send Move", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
             0, 168, y, 90, 24, IDM_SEND_MOVE, font);

    make_ctl("STATIC", "State:", WS_CHILD | WS_VISIBLE | SS_LEFT,
             0, 268, y + 6, 40, 20, 0, font);
    g_hEdtState = make_ctl("EDIT", "startpos", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                           0, 312, y + 2, 110, 24, IDM_EDT_STATE, font);
    make_ctl("BUTTON", "Send State", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
             0, 428, y, 90, 24, IDM_SEND_STATE, font);

    make_ctl("BUTTON", "Offer Draw", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
             0, 528, y, 90, 24, IDM_DRAW_OFFER, font);
    make_ctl("BUTTON", "Accept Draw", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
             0, 624, y, 90, 24, IDM_DRAW_ACCEPT, font);
    make_ctl("BUTTON", "Resign", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
             0, 720, y, 70, 24, IDM_RESIGN, font);

    
    g_hPanel = make_ctl("PasinuxPanel", NULL, WS_CHILD | WS_VISIBLE,
                        0, 8, 110, 820, 180, 0, NULL);

    
    make_ctl("STATIC", "Console log:", WS_CHILD | WS_VISIBLE | SS_LEFT,
             0, 8, 296, 90, 18, 0, font);
    g_hLog = make_ctl("EDIT", NULL,
                      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                      ES_READONLY | ES_AUTOVSCROLL,
                      0, 8, 314, 820, 220, 0, font);
    (void)hInst;
}





static void do_run(void) {
    char buf[16];
    get_edit_text(g_hEdtRunTicks, buf, sizeof(buf));
    request_t r;
    memset(&r, 0, sizeof(r));
    r.action = GUI_ACT_RUN;
    r.ticks = (uint64_t)strtoull(buf, NULL, 10);
    post_request(&r);
}

static void do_spawn(void) {
    request_t r;
    memset(&r, 0, sizeof(r));
    r.action = GUI_ACT_SPAWN;
    get_edit_text(g_hEdtSpawnName, r.text_a, sizeof(r.text_a));
    r.priority = priority_from_index(combo_index(g_hCmbSpawnPri));
    post_request(&r);
}

static void do_chess(gui_action_t act, HWND edit) {
    request_t r;
    memset(&r, 0, sizeof(r));
    r.action = act;
    get_edit_text(edit, r.text_a, sizeof(r.text_a));
    post_request(&r);
}

static void apply_config_controls(void) {
    scheduler_config_t* cfg = scheduler_get_config();
    int idx = combo_index(g_hCmbPolicy);
    cfg->scheduling_policy = (uint8_t)(idx == 1 ? 1 : 0);
    cfg->preemptive = (SendMessageA(g_hChkPreempt, BM_GETCHECK, 0, 0) == BST_CHECKED);
}





static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        draw_panel();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        LPCREATESTRUCTA cs = (LPCREATESTRUCTA)lp;
        g_hMain = hwnd;
        build_ui(hwnd, cs->hInstance);
        return 0;
    }

    case WM_APP_LOGLINE:
        append_log_line((const char*)lp);
        LocalFree((HLOCAL)lp);
        return 0;

    case WM_APP_REFRESH:
        InvalidateRect(g_hPanel, NULL, FALSE);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_STEP: {
            request_t r; memset(&r, 0, sizeof(r));
            r.action = GUI_ACT_STEP;
            post_request(&r);
            break;
        }
        case IDM_RUN:        do_run(); break;
        case IDM_RUN_DEMO: {
            request_t r; memset(&r, 0, sizeof(r));
            r.action = GUI_ACT_RUN_DEMO;
            post_request(&r);
            break;
        }
        case IDM_RESET: {
            request_t r; memset(&r, 0, sizeof(r));
            r.action = GUI_ACT_RESET;
            post_request(&r);
            break;
        }
        case IDM_DUMP: {
            request_t r; memset(&r, 0, sizeof(r));
            r.action = GUI_ACT_DUMP;
            post_request(&r);
            break;
        }
        case IDM_SPAWN:      do_spawn(); break;
        case IDM_SEND_MOVE:  do_chess(GUI_ACT_CHESS_MOVE, g_hEdtMove); break;
        case IDM_SEND_STATE: do_chess(GUI_ACT_CHESS_STATE, g_hEdtState); break;
        case IDM_DRAW_OFFER: {
            request_t r; memset(&r, 0, sizeof(r));
            r.action = GUI_ACT_CHESS_DRAW_OFFER;
            post_request(&r);
            break;
        }
        case IDM_DRAW_ACCEPT: {
            request_t r; memset(&r, 0, sizeof(r));
            r.action = GUI_ACT_CHESS_DRAW_ACCEPT;
            post_request(&r);
            break;
        }
        case IDM_RESIGN: {
            request_t r; memset(&r, 0, sizeof(r));
            r.action = GUI_ACT_CHESS_RESIGN;
            post_request(&r);
            break;
        }
        case IDM_CMB_POLICY:
        case IDM_CHK_PREEMPT:
            if (HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == BN_CLICKED) {
                apply_config_controls();
            }
            break;
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;     

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}





int WINAPI WinMain(HINSTANCE hInst, HINSTANCE prev, LPSTR cmdLine, int show) {
    (void)prev;
    (void)cmdLine;

    
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    InitializeCriticalSection(&g_reqLock);
    InitializeConditionVariable(&g_reqCv);

    
    redirect_stdout_to_pipe();

    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "PasinuxConsole";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExA(&wc);

    
    WNDCLASSEXA wcp;
    memset(&wcp, 0, sizeof(wcp));
    wcp.cbSize = sizeof(wcp);
    wcp.lpfnWndProc = panel_proc;
    wcp.hInstance = hInst;
    wcp.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcp.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcp.lpszClassName = "PasinuxPanel";
    RegisterClassExA(&wcp);

    g_hMain = CreateWindowExA(0, "PasinuxConsole", "pasinux operator console",
                              WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 860, 600,
                              NULL, NULL, hInst, NULL);
    if (!g_hMain) {
        return 1;
    }

    ShowWindow(g_hMain, show);
    UpdateWindow(g_hMain);

    
    CreateThread(NULL, 0, reader_thread, NULL, 0, NULL);
    CreateThread(NULL, 0, worker_thread, NULL, 0, NULL);

    
    {
        request_t r; memset(&r, 0, sizeof(r));
        r.action = GUI_ACT_RESET;
        post_request(&r);
    }
    InvalidateRect(g_hPanel, NULL, FALSE);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    InterlockedExchange(&g_workerStop, 1);
    WakeConditionVariable(&g_reqCv);
    return (int)msg.wParam;
}
