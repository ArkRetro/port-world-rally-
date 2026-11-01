/* 'Crear imagenes PS1.exe': ventana del creador de imagenes de World Rally 1 y 2 para PS1 en Windows (uso personal) -
   arkretro. Elegir la carpeta de las ROM y la de salida, boton Crear: lanza app\python\python.exe app\creador.py
   (creador.py hace todo) sin consola, muestra su salida en el registro y avanza la barra con sus lineas
   '######## n/6'. Al acabar, aviso y la carpeta de salida abierta. Sin dependencias: Win32 y comctl32.
   Compilar (empaquetar.sh): x86_64-w64-mingw32-gcc -municode -mwindows -O2 -s creador.c creador.res -o ... -lcomctl32
   -lole32 -lshell32 -luuid */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <stdio.h>
#include <wchar.h>

enum { ID_ROMS = 101, ID_ROMS_B, ID_SAL, ID_SAL_B, ID_USA, ID_CREAR, ID_LOG, ID_BARRA, ID_ESTADO };
#define WM_LINEA (WM_APP + 1)       /* lParam: texto (malloc) para el registro */
#define WM_FIN (WM_APP + 2)         /* wParam: codigo de salida */
static HWND ven, e_roms, e_sal, b_crear, e_log, barra, estado, c_usa;
static wchar_t dir_app[MAX_PATH];
static HANDLE hilo;
static HFONT fuente, fuente_t;

static void elegir(HWND edit, const wchar_t *titulo)
{
    IFileOpenDialog *d; IShellItem *it; wchar_t *p;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&d))) return;
    IFileOpenDialog_SetOptions(d, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    IFileOpenDialog_SetTitle(d, titulo);
    if (SUCCEEDED(IFileOpenDialog_Show(d, ven)) && SUCCEEDED(IFileOpenDialog_GetResult(d, &it))) {
        if (SUCCEEDED(IShellItem_GetDisplayName(it, SIGDN_FILESYSPATH, &p))) { SetWindowTextW(edit, p); CoTaskMemFree(p); }
        IShellItem_Release(it);
    }
    IFileOpenDialog_Release(d);
}

static void linea(const char *s)    /* UTF-8 -> la ventana (desde el hilo) */
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = malloc((n + 2) * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    PostMessageW(ven, WM_LINEA, 0, (LPARAM)w);
}

static DWORD WINAPI trabajo(LPVOID arg)
{
    wchar_t roms[MAX_PATH], sal[MAX_PATH], cmd[4 * MAX_PATH + 200];
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE rd, wr; STARTUPINFOW si = {sizeof si}; PROCESS_INFORMATION pi; DWORD cod = 1, n;
    char buf[4096], acum[8192]; int na = 0;
    (void)arg;
    GetWindowTextW(e_roms, roms, MAX_PATH); GetWindowTextW(e_sal, sal, MAX_PATH);
    swprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\\app\\python\\python.exe\" -u \"%ls\\app\\creador.py\" \"%ls\" \"%ls\"%ls",
             dir_app, dir_app, roms, sal, SendMessageW(c_usa, BM_GETCHECK, 0, 0) == BST_CHECKED ? L" --regiones USA" : L"");
    CreatePipe(&rd, &wr, &sa, 0); SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    si.hStdOutput = si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");
    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        linea("ERROR: no se pudo arrancar app\\python\\python.exe (falta la carpeta app junto al programa?)");
        CloseHandle(rd); CloseHandle(wr); PostMessageW(ven, WM_FIN, 1, 0); return 0;
    }
    CloseHandle(wr);
    while (ReadFile(rd, buf, sizeof buf, &n, NULL) && n) {
        DWORD i;
        for (i = 0; i < n; i++) {
            if (buf[i] == '\r') continue;
            if (buf[i] == '\n' || na == (int)sizeof acum - 1) { acum[na] = 0; linea(acum); na = 0; }
            else acum[na++] = buf[i];
        }
    }
    if (na) { acum[na] = 0; linea(acum); }
    WaitForSingleObject(pi.hProcess, INFINITE); GetExitCodeProcess(pi.hProcess, &cod);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
    PostMessageW(ven, WM_FIN, cod, 0);
    return 0;
}

static void anade(wchar_t *w)       /* al registro (y la barra si es un paso) */
{
    int n = GetWindowTextLengthW(e_log), paso, total;
    if (n > 400000) { SendMessageW(e_log, EM_SETSEL, 0, 100000); SendMessageW(e_log, EM_REPLACESEL, 0, (LPARAM)L""); n = GetWindowTextLengthW(e_log); }
    SendMessageW(e_log, EM_SETSEL, n, n); SendMessageW(e_log, EM_REPLACESEL, 0, (LPARAM)w);
    SendMessageW(e_log, EM_REPLACESEL, 0, (LPARAM)L"\r\n");
    if (swscanf(w, L"######## %d/%d", &paso, &total) == 2 && total > 0) {
        SendMessageW(barra, PBM_SETPOS, paso * 100 / (total + 1) + 4, 0);
        SetWindowTextW(estado, w + 9);
    } else if (!wcsncmp(w, L"Descargando", 11) || !wcsncmp(w, L"Instalando", 10) || !wcsncmp(w, L"== ", 3)) SetWindowTextW(estado, w);
}

static HWND ctl(const wchar_t *cls, const wchar_t *txt, DWORD st, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(!wcscmp(cls, L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, txt, WS_CHILD | WS_VISIBLE | st, x, y, w, h, ven, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)fuente, TRUE);
    return c;
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE: {
        wchar_t def[MAX_PATH]; HWND t;
        ven = h;
        t = ctl(L"STATIC", L"World Rally 1 y 2 para PlayStation", 0, 16, 12, 668, 30, 0); SendMessageW(t, WM_SETFONT, (WPARAM)fuente_t, TRUE);
        ctl(L"STATIC", L"Crea las im\u00e1genes de CD (.bin/.cue) a partir de tus ROM de MAME. Uso personal: no las compartas.", 0, 16, 46, 668, 36, 0);
        ctl(L"STATIC", L"1. Carpeta con tus ROM: wrally.zip, wrally2.zip (y wrallyd.zip si tienes un romset split) y, si la tienes,"
                       L" la BIOS de PS1 (scph1001.bin, scph101.bin...) para el logo de PlayStation.", 0, 16, 86, 668, 54, 0);
        e_roms = ctl(L"EDIT", L"", ES_AUTOHSCROLL, 16, 142, 558, 26, ID_ROMS);
        ctl(L"BUTTON", L"Elegir...", BS_PUSHBUTTON, 584, 141, 100, 28, ID_ROMS_B);
        ctl(L"STATIC", L"2. Carpeta donde dejar las im\u00e1genes:", 0, 16, 182, 668, 20, 0);
        swprintf(def, MAX_PATH, L"%ls\\Imagenes PS1", dir_app);
        e_sal = ctl(L"EDIT", def, ES_AUTOHSCROLL, 16, 204, 558, 26, ID_SAL);
        ctl(L"BUTTON", L"Elegir...", BS_PUSHBUTTON, 584, 203, 100, 28, ID_SAL_B);
        c_usa = ctl(L"BUTTON", L"Solo versi\u00f3n USA (m\u00e1s r\u00e1pido; sin las europeas)", BS_AUTOCHECKBOX, 16, 248, 420, 22, ID_USA);
        b_crear = ctl(L"BUTTON", L"3. Crear im\u00e1genes", BS_DEFPUSHBUTTON, 464, 242, 220, 36, ID_CREAR);
        barra = ctl(PROGRESS_CLASSW, L"", 0, 16, 292, 668, 18, ID_BARRA); SendMessageW(barra, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        estado = ctl(L"STATIC", L"La primera vez descarga las herramientas (unos 200 MB). Despu\u00e9s tarda unos 20-40 minutos.", 0, 16, 316, 668, 36, ID_ESTADO);
        e_log = ctl(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL, 16, 356, 668, 200, ID_LOG);
        SendMessageW(e_log, EM_SETLIMITTEXT, 0, 0);
        return 0; }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_ROMS_B) elegir(e_roms, L"Carpeta con tus ROM de MAME");
        if (LOWORD(wp) == ID_SAL_B) elegir(e_sal, L"Carpeta donde dejar las im\u00e1genes");
        if (LOWORD(wp) == ID_CREAR && !hilo) {
            wchar_t r[MAX_PATH]; DWORD at;
            GetWindowTextW(e_roms, r, MAX_PATH); at = GetFileAttributesW(r);
            if (!*r || at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY)) {
                MessageBoxW(h, L"Elige primero la carpeta donde est\u00e1n tus ROM (los .zip de MAME).", L"Falta la carpeta de las ROM", MB_ICONWARNING); return 0;
            }
            SetWindowTextW(e_log, L""); SendMessageW(barra, PBM_SETPOS, 1, 0);
            EnableWindow(b_crear, FALSE); SetWindowTextW(b_crear, L"Trabajando...");
            hilo = CreateThread(NULL, 0, trabajo, NULL, 0, NULL);
        }
        return 0;
    case WM_LINEA: anade((wchar_t *)lp); free((void *)lp); return 0;
    case WM_FIN: {
        wchar_t s[MAX_PATH];
        CloseHandle(hilo); hilo = NULL; EnableWindow(b_crear, TRUE); SetWindowTextW(b_crear, L"3. Crear im\u00e1genes");
        if (wp == 0) {
            SendMessageW(barra, PBM_SETPOS, 100, 0); SetWindowTextW(estado, L"\u00a1Listo!");
            GetWindowTextW(e_sal, s, MAX_PATH);
            MessageBoxW(h, L"\u00a1Listo! Las im\u00e1genes est\u00e1n en la carpeta que se abre ahora.\n\n"
                           L"Para grabar un CD: carga el .cue en el programa de grabaci\u00f3n (modo DAO, a 4x-8x).\n"
                           L"Para XStation o PSIO: copia a la tarjeta la carpeta que diga XStation o PSIO.", L"World Rally PS1", MB_ICONINFORMATION);
            ShellExecuteW(h, L"open", s, NULL, NULL, SW_SHOWNORMAL);
        } else {
            SetWindowTextW(estado, L"Ha fallado: mira el final del registro.");
            MessageBoxW(h, L"No se han podido crear las im\u00e1genes. Mira las \u00faltimas l\u00edneas del registro: suelen decir qu\u00e9 falta "
                           L"(una ROM, la conexi\u00f3n a internet...). El registro completo est\u00e1 en C:\\WRPS1\\registro.txt.\n\n"
                           L"Puedes volver a pulsar Crear: sigue donde se qued\u00f3.", L"World Rally PS1", MB_ICONERROR);
        }
        return 0; }
    case WM_CLOSE:
        if (hilo && MessageBoxW(h, L"Se est\u00e1n creando las im\u00e1genes. \u00bfCerrar de todas formas? (la pr\u00f3xima vez seguir\u00e1 donde iba)",
                                L"World Rally PS1", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, PWSTR cmd, int show)
{
    WNDCLASSW wc = {0}; MSG msg; INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_PROGRESS_CLASS}; wchar_t *p; RECT r = {0, 0, 700, 572};
    (void)hp; (void)cmd;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    InitCommonControlsEx(&icc);
    GetModuleFileNameW(NULL, dir_app, MAX_PATH); if ((p = wcsrchr(dir_app, L'\\'))) *p = 0;
    fuente = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    fuente_t = CreateFontW(-24, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    wc.lpfnWndProc = proc; wc.hInstance = hi; wc.lpszClassName = L"WRPS1"; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    CreateWindowW(L"WRPS1", L"World Rally PS1 - Creador de im\u00e1genes", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                  CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, hi, NULL);
    ShowWindow(ven, show); UpdateWindow(ven);
    while (GetMessageW(&msg, NULL, 0, 0)) { if (!IsDialogMessageW(ven, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    return 0;
}
