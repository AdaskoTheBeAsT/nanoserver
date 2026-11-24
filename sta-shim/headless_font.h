// Included by nanoserver_sta_shim.cpp inside its implementation namespace.
// Read machine-installed TrueType faces from the Windows Fonts directory.
namespace HeadlessFont {

constexpr wchar_t kDefaultFile[] = L"lucon.ttf";
constexpr ULONG_PTR kFontPrefix = 0x73000000u;
constexpr ULONG_PTR kDcPrefix = 0x74000000u;
constexpr ULONG_PTR kHandleMask = 0xff000000u;

constexpr DWORD Tag(char a, char b, char c, char d) {
    return static_cast<DWORD>(a) | (static_cast<DWORD>(b) << 8) |
        (static_cast<DWORD>(c) << 16) | (static_cast<DWORD>(d) << 24);
}

struct Bytes {
    const BYTE* ptr = nullptr;
    size_t size = 0;

    Bytes Slice(size_t offset, size_t length) const {
        return ptr && offset <= size && length <= size - offset ?
            Bytes{ptr + offset, length} : Bytes{};
    }

    unsigned U16(size_t at) const {
        const auto b = Slice(at, 2);
        return b.ptr ? (unsigned(b.ptr[0]) << 8) | b.ptr[1] : 0;
    }

    int S16(size_t at) const { return static_cast<SHORT>(U16(at)); }

    DWORD U32(size_t at) const {
        return (static_cast<DWORD>(U16(at)) << 16) | U16(at + 2);
    }
};

class Face {
public:
    Face(const wchar_t* path, size_t remaining) {
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        BY_HANDLE_FILE_INFORMATION info{};
        DWORD size = 0;
        if (GetFileInformationByHandle(file, &info) && !info.nFileSizeHigh &&
            !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            size = info.nFileSizeLow;
        if (size >= 12 && size <= 32 * 1024 * 1024 && size <= remaining) {
            data_.reset(new (std::nothrow) BYTE[size]);
            DWORD read = 0;
            if (data_ && ReadFile(file, data_.get(), size, &read, nullptr) && read == size)
                bytes_ = {data_.get(), size};
        }
        CloseHandle(file);
        if (bytes_.U32(0) != 0x00010000 || !bytes_.Slice(12, bytes_.U16(4) * 16u).ptr) {
            bytes_ = {};
            return;
        }
        head = Table(Tag('h', 'e', 'a', 'd'));
        hhea = Table(Tag('h', 'h', 'e', 'a'));
        os2 = Table(Tag('O', 'S', '/', '2'));
        post = Table(Tag('p', 'o', 's', 't'));
        hmtx = Table(Tag('h', 'm', 't', 'x'));
        cmap = Table(Tag('c', 'm', 'a', 'p'));
        loca = Table(Tag('l', 'o', 'c', 'a'));
        glyf = Table(Tag('g', 'l', 'y', 'f'));
        glyphCount = Table(Tag('m', 'a', 'x', 'p')).U16(4);
        em = head.U16(18);
        const auto locaFormat = head.S16(50);
        const auto horizontalMetrics = hhea.U16(34);
        valid = head.size >= 54 && hhea.size >= 36 && os2.size >= 78 && post.size >= 16 &&
            em >= 16 && glyphCount && horizontalMetrics && horizontalMetrics <= glyphCount &&
            hmtx.size >= horizontalMetrics * 4u + (glyphCount - horizontalMetrics) * 2u &&
            cmap.size >= 4 && (locaFormat == 0 || locaFormat == 1) &&
            loca.size >= (glyphCount + 1u) * (locaFormat ? 4u : 2u) && glyf.ptr &&
            !Table(Tag('f', 'v', 'a', 'r')).ptr;
        if (!valid) return;
        const auto name = Table(Tag('n', 'a', 'm', 'e'));
        const auto records = name.Slice(6, name.U16(2) * 12u);
        const auto strings = name.U16(4);
        if (!records.ptr || strings < 6u + records.size || strings > name.size) { valid = false; return; }
        ReadName(name, records, strings, 1, family);
        ReadName(name, records, strings, 2, style);
        ReadName(name, records, strings, 4, fullName);
        valid = family[0] && style[0] && fullName[0];
        weight = (std::clamp)(os2.U16(4), 1u, 1000u);
        italic = (os2.U16(62) & 0x0201u) != 0 || (head.U16(44) & 2u) != 0;
        fixedPitch = post.U32(12) != 0;
    }

    Bytes Table(DWORD tag) const {
        if (!tag) return bytes_;
        for (unsigned i = 0; i < bytes_.U16(4); ++i) {
            const size_t at = 12 + i * 16;
            const BYTE* s = bytes_.ptr + at;
            if (Tag(s[0], s[1], s[2], s[3]) == tag)
                return bytes_.Slice(bytes_.U32(at + 8), bytes_.U32(at + 12));
        }
        return {};
    }

    unsigned Glyph(UINT ch) const {
        const unsigned count = cmap.U16(2);
        if (!cmap.Slice(4, count * 8u).ptr) return 0;
        // Prefer the full Unicode cmap; otherwise use the BMP Windows/Unicode records.
        for (unsigned i = 0; i < count; ++i) {
            const size_t at = 4 + i * 8;
            if (cmap.U16(at) != 0 && (cmap.U16(at) != 3 || cmap.U16(at + 2) != 10)) continue;
            const size_t start = cmap.U32(at + 4);
            if (!cmap.Slice(start, 16).ptr || cmap.U16(start) != 12) continue;
            const auto sub = cmap.Slice(start, cmap.U32(start + 4));
            const unsigned groups = sub.U32(12);
            if (sub.size < 16 || groups > (sub.size - 16) / 12) continue;
            unsigned lo = 0, hi = groups;
            while (lo < hi) {
                const unsigned mid = lo + (hi - lo) / 2;
                const size_t offset = 16 + mid * 12u;
                const auto first = sub.U32(offset), last = sub.U32(offset + 4);
                if (ch < first) hi = mid;
                else if (ch > last) lo = mid + 1;
                else {
                    const uint64_t glyph = uint64_t(sub.U32(offset + 8)) + ch - first;
                    if (glyph < glyphCount) return static_cast<unsigned>(glyph);
                    break;
                }
            }
        }
        if (ch > MAXWORD) return 0;
        for (unsigned i = 0; i < count; ++i) {
            const size_t at = 4 + i * 8;
            if (cmap.U16(at) != 0 && (cmap.U16(at) != 3 ||
                (cmap.U16(at + 2) != 1 && cmap.U16(at + 2) != 10))) continue;
            const auto start = cmap.U32(at + 4);
            if (!cmap.Slice(start, 4).ptr) continue;
            auto sub = cmap.Slice(start, cmap.U16(static_cast<size_t>(start) + 2));
            if (sub.U16(0) != 4) continue;
            unsigned segments = sub.U16(6) / 2;
            if (!sub.Slice(16, segments * 8u).ptr) continue;
            for (unsigned j = 0; j < segments; ++j) {
                if (ch > sub.U16(14 + j * 2u)) continue;
                unsigned low = sub.U16(16 + segments * 2u + j * 2u);
                if (ch < low) break;
                int delta = sub.S16(16 + segments * 4u + j * 2u);
                size_t entry = 16 + segments * 6u + j * 2u;
                unsigned offset = sub.U16(entry);
                unsigned glyph = offset ? sub.U16(entry + offset + (ch - low) * 2u) : ch;
                glyph = glyph ? static_cast<WORD>(glyph + delta) : 0;
                if (glyph && glyph < glyphCount) return glyph;
                break;
            }
        }
        return 0;
    }

    unsigned Advance(unsigned glyph) const {
        return hmtx.U16(4 * (std::min)(glyph, hhea.U16(34) - 1));
    }

    Bytes GlyphData(unsigned glyph) const {
        if (glyph >= glyphCount) return {};
        bool wide = head.S16(50) != 0;
        auto offset = [this, wide](unsigned index) {
            return wide ? loca.U32(index * 4u) : loca.U16(index * 2u) * 2u;
        };
        DWORD from = offset(glyph), to = offset(glyph + 1);
        return to >= from && to - from >= 10 ? glyf.Slice(from, to - from) : Bytes{};
    }

    Bytes head, hhea, os2, post, hmtx, cmap, loca, glyf;
    unsigned em = 0, glyphCount = 0;
    wchar_t family[LF_FACESIZE]{}, style[LF_FACESIZE]{}, fullName[LF_FULLFACESIZE]{};
    unsigned weight = FW_NORMAL;
    bool italic = false, fixedPitch = false;
    bool valid = false;

private:
    template <size_t N>
    static void ReadName(Bytes table, Bytes records, size_t strings, unsigned id, wchar_t (&name)[N]) {
        unsigned best = 0;
        for (size_t i = 0; i < records.size; i += 12) {
            const auto platform = records.U16(i), encoding = records.U16(i + 2);
            if (records.U16(i + 6) != id ||
                (platform != 0 && !(platform == 3 && (encoding == 1 || encoding == 10)))) continue;
            unsigned score = platform == 3 ? (records.U16(i + 4) == 0x0409 ? 3 : 2) : 1;
            if (score <= best) continue;
            const size_t size = records.U16(i + 8);
            auto str = table.Slice(strings + records.U16(i + 10), size);
            if (!str.ptr || !size || (size & 1) || size / 2 >= N) continue;
            bool printable = true;
            for (size_t j = 0; j < size; j += 2) if (str.U16(j) < 32) { printable = false; break; }
            if (!printable) continue;
            for (size_t j = 0; j < size / 2; ++j) name[j] = static_cast<wchar_t>(str.U16(j * 2));
            name[size / 2] = 0;
            best = score;
        }
    }

    std::unique_ptr<BYTE[]> data_;
    Bytes bytes_;
};

class Catalog {
public:
    Catalog() {
        wchar_t root[MAX_PATH]{};
        UINT length = GetWindowsDirectoryW(root, ARRAYSIZE(root));
        if (!length || length >= ARRAYSIZE(root) || wcscat_s(root, L"\\Fonts\\")) return;
        // Keep the stock fallback stable even on images with other families installed.
        Add(root, kDefaultFile);
        HMODULE advapi = LoadLibraryExW(L"advapi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!advapi) return;
        struct Library {
            HMODULE handle;
            ~Library() { FreeLibrary(handle); }
        } module{advapi};
        auto open = reinterpret_cast<decltype(&RegOpenKeyExW)>(GetProcAddress(advapi, "RegOpenKeyExW"));
        auto enumerate = reinterpret_cast<decltype(&RegEnumValueW)>(GetProcAddress(advapi, "RegEnumValueW"));
        auto close = reinterpret_cast<decltype(&RegCloseKey)>(GetProcAddress(advapi, "RegCloseKey"));
        HKEY key = nullptr;
        if (!open || !enumerate || !close || open(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0,
                KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return;
        for (DWORD index = 0; index < 4096 && faces.size() < 512; ++index) {
            wchar_t valueName[256], filename[MAX_PATH]{};
            DWORD nameSize = ARRAYSIZE(valueName), dataSize = sizeof(filename) - sizeof(wchar_t), type = 0;
            LSTATUS result = enumerate(key, index, valueName, &nameSize, nullptr, &type,
                reinterpret_cast<BYTE*>(filename), &dataSize);
            if (result == ERROR_NO_MORE_ITEMS) break;
            if (result != ERROR_SUCCESS || type != REG_SZ || !dataSize || dataSize % sizeof(wchar_t)) continue;
            const size_t prefix = wcslen(root);
            const wchar_t* leaf = !_wcsnicmp(filename, root, prefix) ? filename + prefix : filename;
            Add(root, leaf);
        }
        close(key);
    }

    std::vector<std::unique_ptr<const Face>> faces;

private:
    void Add(const wchar_t* root, const wchar_t* file) {
        const size_t length = wcslen(file);
        if (length < 5 || _wcsicmp(file + length - 4, L".ttf") ||
            wcspbrk(file, L"\\/:*?\"<>|") || file[0] == L'.') return;
        for (const auto& seen : files_) if (!_wcsicmp(seen.c_str(), file)) return;
        wchar_t path[MAX_PATH];
        if (swprintf_s(path, L"%ls%ls", root, file) < 0) return;
        try {
            files_.emplace_back(file);
            auto face = std::make_unique<Face>(path, remaining_);
            if (!face->valid) return;
            const size_t size = face->Table(0).size;
            faces.push_back(std::move(face));
            remaining_ -= size;
        } catch (const std::bad_alloc&) {
            // Keep already loaded faces available if memory is tight.
        }
    }

    size_t remaining_ = 128 * 1024 * 1024;
    std::vector<std::wstring> files_;
};

const Catalog* InstalledFonts() {
    static const bool enabled = [] {
        wchar_t value[2]{};
        return GetEnvironmentVariableW(L"NANOSERVER_ENABLE_HEADLESS_FONTS", value, 2) == 1 && value[0] == L'1';
    }();
    if (!enabled) return nullptr;
    static const Catalog catalog;
    return catalog.faces.empty() ? nullptr : &catalog;
}

const Face* MatchFont(const Catalog& catalog, const LOGFONTW& request) {
    const Face* match = nullptr;
    unsigned best = UINT_MAX;
    const unsigned weight = request.lfWeight ? (std::clamp)(request.lfWeight, 1L, 1000L) : FW_NORMAL;
    for (const auto& face : catalog.faces) {
        if (request.lfFaceName[0] && _wcsnicmp(request.lfFaceName, face->family, LF_FACESIZE)) continue;
        if (!request.lfFaceName[0] && _wcsicmp(face->family, catalog.faces.front()->family)) continue;
        const unsigned distance = weight > face->weight ? weight - face->weight : face->weight - weight;
        const unsigned score = distance + ((request.lfItalic != 0) != face->italic ? 1000u : 0u);
        if (score < best) { best = score; match = face.get(); }
    }
    return match ? match : catalog.faces.front().get();
}

struct DC {
    HGDIOBJ selected = nullptr;
};

SRWLOCK lock = SRWLOCK_INIT;
struct Lock {
    Lock() { AcquireSRWLockExclusive(&lock); }
    ~Lock() { ReleaseSRWLockExclusive(&lock); }
};
std::vector<std::pair<HDC, DC>> dcs;
struct Font {
    LOGFONTW request;
    const Face* face;
};
std::vector<std::pair<HFONT, Font>> fonts;
ULONG_PTR serial = 0;
DC display;

ULONG_PTR NextHandle(ULONG_PTR prefix) {
    if (serial >= 0x00ffffffu) return 0;
    return prefix | ++serial;
}

HGDIOBJ Stock() {
    return reinterpret_cast<HGDIOBJ>(static_cast<ULONG_PTR>(0x53484F00u + SYSTEM_FONT));
}

DC* FindDC(HDC dc) {
    if (dc == kHeadlessDisplayDC || dc == kHeadlessCompatibleDC) return &display;
    for (auto& item : dcs) if (item.first == dc) return &item.second;
    return nullptr;
}

bool IsDC(HDC dc) {
    Lock hold;
    return FindDC(dc) != nullptr;
}

HDC CreateDC() {
    Lock hold;
    auto dc = reinterpret_cast<HDC>(NextHandle(kDcPrefix));
    if (dc) {
        try { dcs.emplace_back(dc, DC{}); }
        catch (const std::bad_alloc&) { dc = nullptr; }
    }
    if (!dc) SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return dc;
}

bool DeleteDC(HDC dc) {
    Lock hold;
    auto found = std::find_if(dcs.begin(), dcs.end(), [dc](const auto& item) { return item.first == dc; });
    if (found == dcs.end()) return false;
    dcs.erase(found);
    return true;
}

HFONT CreateFont(const Catalog& catalog, const LOGFONTW* data) {
    if (!data) { SetLastError(ERROR_INVALID_PARAMETER); return nullptr; }
    Lock hold;
    auto font = reinterpret_cast<HFONT>(NextHandle(kFontPrefix));
    if (font) {
        try { fonts.emplace_back(font, Font{*data, MatchFont(catalog, *data)}); }
        catch (const std::bad_alloc&) { font = nullptr; }
    }
    if (!font) SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return font;
}

bool IsFontToken(HGDIOBJ object) {
    const auto value = reinterpret_cast<ULONG_PTR>(object);
    return value <= MAXDWORD && (value & kHandleMask) == kFontPrefix;
}

const Font* FindFont(HGDIOBJ object) {
    for (const auto& item : fonts) if (item.first == object) return &item.second;
    return nullptr;
}

HGDIOBJ Current(HDC dc) {
    Lock hold;
    const DC* state = FindDC(dc);
    return state ? (FindFont(state->selected) ? state->selected : Stock()) : nullptr;
}

bool Select(HDC dc, HGDIOBJ object, HGDIOBJ& previous) {
    Lock hold;
    const auto stock = reinterpret_cast<ULONG_PTR>(object) - 0x53484f00u;
    bool stockFont = stock == OEM_FIXED_FONT || stock == ANSI_FIXED_FONT ||
        stock == ANSI_VAR_FONT || stock == SYSTEM_FONT || stock == DEVICE_DEFAULT_FONT ||
        stock == SYSTEM_FIXED_FONT || stock == DEFAULT_GUI_FONT;
    if (!stockFont && !IsFontToken(object)) return false;
    DC* state = FindDC(dc);
    if (!state || (!stockFont && !FindFont(object))) {
        SetLastError(ERROR_INVALID_HANDLE);
        previous = nullptr;
    } else {
        previous = FindFont(state->selected) ? state->selected : Stock();
        state->selected = stockFont ? nullptr : object;
    }
    return true;
}

BOOL DeleteFont(HGDIOBJ object) {
    Lock hold;
    for (auto it = fonts.begin(); it != fonts.end(); ++it) {
        if (it->first != object) continue;
        if (display.selected == object || std::any_of(dcs.begin(), dcs.end(),
                [object](const auto& item) { return item.second.selected == object; })) return FALSE;
        fonts.erase(it);
        return TRUE;
    }
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

bool GetLogicalFont(HGDIOBJ object, LOGFONTW& font) {
    Lock hold;
    if (const auto* found = FindFont(object)) { font = found->request; return true; }
    return false;
}

struct Selection {
    const Face* face = nullptr;
    int px = 0;
};

Selection SelectedFont(const Catalog& catalog, HDC dc) {
    Lock hold;
    const auto* state = FindDC(dc);
    if (!state) return {};
    const auto* font = FindFont(state->selected);
    const Face* face = font ? font->face : catalog.faces.front().get();
    if (!font || !font->request.lfHeight) return {face, 12};
    const auto height = (std::clamp)(font->request.lfHeight, -32767L, 32767L);
    if (height < 0) return {face, static_cast<int>(-height)};
    return {face, (std::max)(1, MulDiv(height, face->em,
        (std::max)(1, face->hhea.S16(4) - face->hhea.S16(6))))};
}

TEXTMETRICW Metrics(const Face& face, int px) {
    TEXTMETRICW tm{};
    tm.tmAscent = MulDiv(face.hhea.S16(4), px, face.em);
    tm.tmDescent = MulDiv(-face.hhea.S16(6), px, face.em);
    tm.tmHeight = tm.tmAscent + tm.tmDescent;
    tm.tmInternalLeading = (std::max)(0L, tm.tmHeight - px);
    tm.tmExternalLeading = MulDiv(face.hhea.S16(8), px, face.em);
    tm.tmAveCharWidth = MulDiv(face.os2.S16(2), px, face.em);
    tm.tmMaxCharWidth = MulDiv(face.hhea.U16(10), px, face.em);
    tm.tmWeight = face.weight;
    tm.tmItalic = face.italic;
    tm.tmDigitizedAspectX = tm.tmDigitizedAspectY = 96;
    tm.tmFirstChar = static_cast<WCHAR>(face.os2.U16(64));
    tm.tmLastChar = static_cast<WCHAR>(face.os2.U16(66));
    tm.tmDefaultChar = L'?';
    tm.tmBreakChar = L' ';
    tm.tmPitchAndFamily = TMPF_TRUETYPE | TMPF_VECTOR |
        (face.fixedPitch ? FF_MODERN : TMPF_FIXED_PITCH);
    tm.tmCharSet = ANSI_CHARSET;
    return tm;
}

ABC Widths(const Face& face, unsigned glyph, int px) {
    const auto box = face.GlyphData(glyph);
    const int left = MulDiv(box.S16(2), px, face.em);
    const int right = MulDiv(box.S16(6), px, face.em);
    const int advance = MulDiv(face.Advance(glyph), px, face.em);
    return {left, static_cast<UINT>((std::max)(0, right - left)), advance - right};
}

struct OutlinePoint {
    double x = 0, y = 0;
    bool on = false;
};

struct Outline {
    std::vector<OutlinePoint> points;
    std::vector<unsigned> ends;
};

// Decode unhinted TrueType contours. Bound recursion and total work even if the
// installed face is damaged. Component offsets can be coordinates or point indices.
bool ReadOutline(const Face& face, unsigned glyph, unsigned depth, unsigned& budget, Outline& result) {
    if (!budget || depth == 8 || glyph >= face.glyphCount) return false;
    --budget;
    const Bytes data = face.GlyphData(glyph);
    if (!data.ptr) return true; // Space and other empty glyphs have no contours.
    const int contours = data.S16(0);
    size_t at = 10;
    auto take = [&](unsigned n) {
        Bytes part = data.Slice(at, n);
        at += n;
        return part;
    };
    if (contours >= 0) {
        auto ends = take(contours * 2u);
        auto instructions = take(2);
        if (!ends.ptr || !instructions.ptr || !take(instructions.U16(0)).ptr) return false;
        if (!contours) return true;
        unsigned count = ends.U16((contours - 1) * 2u) + 1;
        if (count > 4096) return false;
        for (int i = 0; i < contours; ++i) {
            unsigned end = ends.U16(i * 2u);
            if (end >= count || (i && end <= result.ends.back())) return false;
            result.ends.push_back(end);
        }
        std::vector<BYTE> flags;
        flags.reserve(count);
        while (flags.size() < count) {
            auto f = take(1);
            if (!f.ptr) return false;
            unsigned repeat = 1;
            if (*f.ptr & 8) {
                auto r = take(1);
                if (!r.ptr) return false;
                repeat += *r.ptr;
            }
            if (repeat > count - flags.size()) return false;
            flags.insert(flags.end(), repeat, *f.ptr);
        }
        result.points.resize(count);
        for (unsigned axis = 0; axis < 2; ++axis) {
            int coordinate = 0;
            for (unsigned i = 0; i < count; ++i) {
                const bool positive = (flags[i] & (16 << axis)) != 0;
                if (flags[i] & (2 << axis)) {
                    auto delta = take(1);
                    if (!delta.ptr) return false;
                    coordinate += positive ? int(*delta.ptr) : -int(*delta.ptr);
                } else if (!positive) {
                    auto delta = take(2);
                    if (!delta.ptr) return false;
                    coordinate += delta.S16(0);
                }
                if (coordinate < -32768 || coordinate > 32767) return false;
                if (!axis) {
                    result.points[i].x = coordinate;
                    result.points[i].on = (flags[i] & 1) != 0;
                } else {
                    result.points[i].y = coordinate;
                }
            }
        }
        return true;
    }

    unsigned flags;
    do {
        auto component = take(4);
        if (!component.ptr) return false;
        flags = component.U16(0);
        bool words = (flags & 1) != 0;
        bool coordinates = (flags & 2) != 0;
        auto args = take(words ? 4 : 2);
        if (!args.ptr) return false;
        double xx = 1, xy = 0, yx = 0, yy = 1;
        if (flags & 8) {
            auto t = take(2);
            if (!t.ptr) return false;
            xx = yy = t.S16(0) / 16384.0;
        } else if (flags & 64) {
            auto t = take(4);
            if (!t.ptr) return false;
            xx = t.S16(0) / 16384.0;
            yy = t.S16(2) / 16384.0;
        } else if (flags & 128) {
            auto t = take(8);
            if (!t.ptr) return false;
            xx = t.S16(0) / 16384.0;
            yx = t.S16(2) / 16384.0;
            xy = t.S16(4) / 16384.0;
            yy = t.S16(6) / 16384.0;
        }
        Outline child;
        if (!ReadOutline(face, component.U16(2), depth + 1, budget, child) ||
            child.points.size() > 4096 - result.points.size()) return false;
        for (auto& p : child.points) p = {xx*p.x + xy*p.y, yx*p.x + yy*p.y, p.on};
        double dx, dy;
        if (coordinates) {
            dx = words ? args.S16(0) : static_cast<signed char>(args.ptr[0]);
            dy = words ? args.S16(2) : static_cast<signed char>(args.ptr[1]);
            if (flags & 2048) {
                const double x = xx*dx + xy*dy;
                dy = yx*dx + yy*dy;
                dx = x;
            }
        } else {
            unsigned a = words ? args.U16(0) : args.ptr[0];
            unsigned b = words ? args.U16(2) : args.ptr[1];
            if (a >= result.points.size() || b >= child.points.size()) return false;
            dx = result.points[a].x - child.points[b].x;
            dy = result.points[a].y - child.points[b].y;
        }
        unsigned base = static_cast<unsigned>(result.points.size());
        for (auto end : child.ends) result.ends.push_back(base + end);
        for (const auto& p : child.points) result.points.push_back({p.x + dx, p.y + dy, p.on});
    } while (flags & 32);
    return true;
}

bool NativeOutline(const Face& face, unsigned glyph, int px, std::vector<BYTE>& bytes) {
    Outline outline;
    unsigned budget = 128;
    if (!ReadOutline(face, glyph, 0, budget, outline)) return false;
    auto point = [px, &face](const OutlinePoint& p) {
        auto fixed = [px, &face](double value) {
            const double scaled = (std::clamp)(value * px / face.em, -32768.0, 32767.99998);
            const auto packed = static_cast<LONG>(scaled * 65536);
            return FIXED{static_cast<WORD>(packed), static_cast<SHORT>(packed >> 16)};
        };
        return POINTFX{fixed(p.x), fixed(p.y)};
    };
    auto middle = [](const OutlinePoint& a, const OutlinePoint& b) {
        return OutlinePoint{(a.x + b.x) / 2, (a.y + b.y) / 2, true};
    };
    auto append = [&bytes](const auto& item, size_t size) {
        auto begin = reinterpret_cast<const BYTE*>(&item);
        bytes.insert(bytes.end(), begin, begin + size);
    };
    unsigned first = 0;
    for (unsigned last : outline.ends) {
        const auto& front = outline.points[first];
        const auto& back = outline.points[last];
        unsigned next = front.on ? first + 1 : first;
        OutlinePoint start = front.on ? front : (back.on ? back : middle(front, back));
        const size_t polygon = bytes.size();
        TTPOLYGONHEADER header{0, TT_POLYGON_TYPE, point(start)};
        append(header, sizeof(header));
        while (next <= last) {
            auto current = outline.points[next++];
            constexpr size_t headerSize = offsetof(TTPOLYCURVE, apfx);
            TTPOLYCURVE curve{};
            curve.wType = current.on ? TT_PRIM_LINE : TT_PRIM_QSPLINE;
            curve.cpfx = current.on ? 1 : 2;
            append(curve, headerSize);
            append(point(current), sizeof(POINTFX));
            if (!current.on) {
                auto following = next <= last ? outline.points[next] : start;
                if (following.on && next <= last) ++next;
                append(point(following.on ? following : middle(current, following)), sizeof(POINTFX));
            }
        }
        header.cb = static_cast<DWORD>(bytes.size() - polygon);
        memcpy(bytes.data() + polygon, &header, sizeof(header));
        first = last + 1;
    }
    return true;
}

} // namespace HeadlessFont
