#pragma once
// Included inside the shim namespace after the window and extra-byte helpers.
// Modeless dialogs provide lifecycle and controls only, not desktop rendering.

struct DialogValue {
    std::wstring Text;
    WORD Ordinal = 0;
    bool IsOrdinal = false;

    LPCWSTR Pointer() const { return IsOrdinal ? MAKEINTRESOURCEW(Ordinal) : Text.c_str(); }
};

struct DialogCursor {
    const BYTE* Next = nullptr;
    const BYTE* End = nullptr;
    bool Valid = false;

    explicit DialogCursor(const void* source) {
        MEMORY_BASIC_INFORMATION memory{};
        Valid = source && VirtualQuery(source, &memory, sizeof(memory)) == sizeof(memory) &&
            memory.State == MEM_COMMIT && !(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS));
        Next = static_cast<const BYTE*>(source);
        if (Valid) End = static_cast<const BYTE*>(memory.BaseAddress) + memory.RegionSize;
    }

    template<typename T> T Read() {
        T value{};
        if (!Valid || Next > End || sizeof(T) > static_cast<size_t>(End - Next)) { Valid = false; return value; }
        memcpy(&value, Next, sizeof(value));
        Next += sizeof(value);
        return value;
    }

    void Skip(size_t bytes) {
        if (!Valid || Next > End || bytes > static_cast<size_t>(End - Next)) { Valid = false; return; }
        Next += bytes;
    }

    void Align() { Skip((4 - (reinterpret_cast<ULONG_PTR>(Next) & 3)) & 3); }

    DialogValue Value() {
        DialogValue result;
        WORD value = Read<WORD>();
        if (value == 0xFFFF) { result.IsOrdinal = true; result.Ordinal = Read<WORD>(); return result; }
        while (Valid && value) { result.Text.push_back(static_cast<wchar_t>(value)); value = Read<WORD>(); }
        return result;
    }
};

struct DialogItem {
    DWORD Style = 0, ExStyle = 0, Id = 0;
    short X = 0, Y = 0, Width = 0, Height = 0;
    DialogValue Class, Title;
    LPVOID CreationData = nullptr;
};

std::wstring DialogClass(const DialogValue& value, bool root) {
    if (!value.IsOrdinal) return value.Text.empty() && root ? L"#32770" : value.Text;
    const wchar_t* const controls[] = {L"BUTTON", L"EDIT", L"STATIC", L"LISTBOX", L"SCROLLBAR", L"COMBOBOX"};
    if (!root && value.Ordinal >= 0x80 && value.Ordinal <= 0x85) return controls[value.Ordinal - 0x80];
    return L"#" + std::to_wstring(value.Ordinal);
}

HWND WINAPI Shim_GetDlgItem(HWND parent, int id) {
    ScopedLock lock(g_WindowLock);
    if (!FindWindowLocked(parent)) { SetLastError(kErrorInvalidWindowHandle); return nullptr; }
    for (const auto& window : g_Windows) {
        if (window.Parent == parent && static_cast<int>(reinterpret_cast<INT_PTR>(window.Menu)) == id)
            return window.HWnd;
    }
    return nullptr;
}

int WINAPI Shim_GetDlgCtrlID(HWND window) {
    return static_cast<int>(WindowLongCommon<LONG_PTR>(window, kGwlpId, false));
}

LRESULT WINAPI Shim_DefDlgProcW(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto proc = reinterpret_cast<DLGPROC>(WindowLongCommon<LONG_PTR>(window, DWLP_DLGPROC, false));
    if (proc) {
        WindowLongCommon<LONG_PTR>(window, DWLP_MSGRESULT, true, 0);
        INT_PTR handled = proc(window, message, wParam, lParam);
        if (message == WM_INITDIALOG) return handled;
        if (handled) return WindowLongCommon<LONG_PTR>(window, DWLP_MSGRESULT, false);
    }
    return Shim_DefWindowProcW(window, message, wParam, lParam);
}

BOOL WINAPI Shim_MapDialogRect(HWND window, LPRECT rect) {
    if (!rect || !Shim_IsWindow(window)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    rect->left = MulDiv(rect->left, 8, 4); rect->right = MulDiv(rect->right, 8, 4);
    rect->top = MulDiv(rect->top, 16, 8); rect->bottom = MulDiv(rect->bottom, 16, 8);
    return TRUE;
}

HWND CreateHeadlessDialog(HINSTANCE instance, LPCDLGTEMPLATE source, HWND parent, DLGPROC proc,
                          LPARAM parameter, bool ansi) {
    DialogCursor cursor(source);
    WORD version = cursor.Read<WORD>(), signature = cursor.Read<WORD>();
    bool extended = signature == 0xFFFF;
    cursor.Next = static_cast<const BYTE*>(static_cast<const void*>(source));
    DialogItem dialog;
    WORD count = 0;
    if (extended) {
        if (version != 1) { SetLastError(ERROR_INVALID_DATA); return nullptr; }
        cursor.Skip(4);
        cursor.Read<DWORD>(); // help ID
        dialog.ExStyle = cursor.Read<DWORD>();
        dialog.Style = cursor.Read<DWORD>();
    } else {
        dialog.Style = cursor.Read<DWORD>();
        dialog.ExStyle = cursor.Read<DWORD>();
    }
    count = cursor.Read<WORD>();
    dialog.X = cursor.Read<short>(); dialog.Y = cursor.Read<short>();
    dialog.Width = cursor.Read<short>(); dialog.Height = cursor.Read<short>();
    DialogValue menu = cursor.Value();
    dialog.Class = cursor.Value();
    dialog.Title = cursor.Value();
    if (dialog.Style & DS_SETFONT) {
        cursor.Read<WORD>();
        if (extended) { cursor.Read<WORD>(); cursor.Read<BYTE>(); cursor.Read<BYTE>(); }
        cursor.Value();
    }
    std::vector<DialogItem> controls;
    for (WORD i = 0; i < count && cursor.Valid; ++i) {
        cursor.Align();
        DialogItem item;
        if (extended) {
            cursor.Read<DWORD>(); // help ID
            item.ExStyle = cursor.Read<DWORD>();
            item.Style = cursor.Read<DWORD>();
        } else {
            item.Style = cursor.Read<DWORD>();
            item.ExStyle = cursor.Read<DWORD>();
        }
        item.X = cursor.Read<short>(); item.Y = cursor.Read<short>();
        item.Width = cursor.Read<short>(); item.Height = cursor.Read<short>();
        item.Id = extended ? cursor.Read<DWORD>() : cursor.Read<WORD>();
        item.Class = cursor.Value();
        item.Title = cursor.Value();
        const BYTE* data = cursor.Next;
        WORD size = cursor.Read<WORD>();
        if (size) {
            // Windows x86/x64 controls count payload bytes in both formats and
            // pass the size-prefixed array to WM_CREATE (real-import regression).
            item.CreationData = const_cast<BYTE*>(data);
            cursor.Skip(size);
        }
        controls.push_back(std::move(item));
    }
    if (!cursor.Valid) { SetLastError(ERROR_INVALID_DATA); return nullptr; }
    if (menu.IsOrdinal || !menu.Text.empty()) { SetLastError(ERROR_NOT_SUPPORTED); return nullptr; }
    std::wstring name = DialogClass(dialog.Class, true);
    HWND window = CreateWindowCommon(dialog.ExStyle, name.c_str(), dialog.Title.Pointer(),
        dialog.Style & ~WS_VISIBLE, dialog.X * 2, dialog.Y * 2, dialog.Width * 2, dialog.Height * 2,
        parent, nullptr, instance, nullptr, ansi);
    if (!window) return nullptr;
    WindowLongCommon<LONG_PTR>(window, DWLP_DLGPROC, true, reinterpret_cast<LONG_PTR>(proc));
    for (const auto& item : controls) {
        name = DialogClass(item.Class, false);
        HWND control = CreateWindowCommon(item.ExStyle, name.c_str(), item.Title.Pointer(),
            item.Style | WS_CHILD, item.X * 2, item.Y * 2, item.Width * 2, item.Height * 2,
            window, reinterpret_cast<HMENU>(static_cast<ULONG_PTR>(item.Id)), instance, item.CreationData, ansi);
        if (!control) { Shim_DestroyWindow(window); return nullptr; }
    }
    HWND focus = nullptr;
    {
        ScopedLock lock(g_WindowLock);
        for (const auto& item : g_Windows) {
            if (item.Parent == window && (item.Style & WS_TABSTOP) && !(item.Style & WS_DISABLED)) {
                focus = item.HWnd; break;
            }
        }
    }
    InvokeWindowProc(window, WM_INITDIALOG, reinterpret_cast<WPARAM>(focus), parameter);
    if (dialog.Style & WS_VISIBLE) {
        ScopedLock lock(g_WindowLock);
        if (auto entry = FindWindowLocked(window)) entry->Style |= WS_VISIBLE;
    }
    return Shim_IsWindow(window) ? window : nullptr;
}

HWND WINAPI Shim_CreateDialogIndirectParamW(HINSTANCE instance, LPCDLGTEMPLATE source, HWND parent,
                                            DLGPROC proc, LPARAM parameter) {
    return CreateHeadlessDialog(instance, source, parent, proc, parameter, false);
}

HWND WINAPI Shim_CreateDialogIndirectParamA(HINSTANCE instance, LPCDLGTEMPLATE source, HWND parent,
                                            DLGPROC proc, LPARAM parameter) {
    return CreateHeadlessDialog(instance, source, parent, proc, parameter, true);
}
