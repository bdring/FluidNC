#if (defined _WIN32) || (defined _WIN64)

// Based on: https://stackoverflow.com/questions/6205981/windows-c-stack-trace-from-a-running-app
// ... but with a bunch of modifications.

#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <string>
#    include <sstream>
#    include <vector>
#    include <Psapi.h>
#    include <algorithm>
#    include <iterator>
#    include <exception>

// Thanks, Jerry Coffin.

#    pragma comment(lib, "psapi.lib")
#    pragma comment(lib, "dbghelp.lib")

// Some versions of imagehlp.dll lack the proper packing directives themselves
// so we need to do it.
#    pragma pack(push, before_imagehlp, 8)
#    include <imagehlp.h>
#    pragma pack(pop, before_imagehlp)

struct module_data {
    std::string image_name;
    std::string module_name;
    void*       base_address;
    DWORD       load_size;
};

class symbol {
    typedef IMAGEHLP_SYMBOL64 sym_type;
    sym_type*                 sym;
    static const int          max_name_len = 1024;

public:
    symbol(HANDLE process, DWORD64 address) : sym((sym_type*)::operator new(sizeof(*sym) + max_name_len)) {
        memset(sym, '\0', sizeof(*sym) + max_name_len);
        sym->SizeOfStruct  = sizeof(*sym);
        sym->MaxNameLength = max_name_len;
        DWORD64 displacement;

        SymGetSymFromAddr64(process, address, &displacement, sym);
    }

    std::string name() { return std::string(sym->Name); }
    std::string undecorated_name() {
        if (*sym->Name == '\0') {
            return "Unknown";
        }

        std::vector<char> und_name(max_name_len);
        UnDecorateSymbolName(sym->Name, &und_name[0], max_name_len, UNDNAME_COMPLETE);
        return std::string(&und_name[0], strlen(&und_name[0]));
    }
};

class get_mod_info {
    HANDLE           process;
    static const int buffer_length = 32768;

public:
    get_mod_info(HANDLE h) : process(h) {}

    module_data operator()(HMODULE module) {
        module_data ret;
        WCHAR       temp[buffer_length];
        char        temp2[buffer_length];
        MODULEINFO  mi;

        GetModuleInformation(process, module, &mi, sizeof(mi));
        ret.base_address = mi.lpBaseOfDll;
        ret.load_size    = mi.SizeOfImage;

        GetModuleFileNameExW(process, module, temp, sizeof(temp) / sizeof(WCHAR));
        for (int i = 0; i < buffer_length; ++i) {
            temp2[i] = char(temp[i]);
        }
        ret.image_name = temp2;
        GetModuleBaseNameW(process, module, temp, sizeof(temp));
        for (int i = 0; i < buffer_length; ++i) {
            temp2[i] = char(temp[i]);
        }
        ret.module_name = temp2;
        std::vector<char> img(ret.image_name.begin(), ret.image_name.end());
        std::vector<char> mod(ret.module_name.begin(), ret.module_name.end());
        SymLoadModule64(process, 0, &img[0], &mod[0], (DWORD64)ret.base_address, ret.load_size);
        return ret;
    }
};

void DumpStackTrace(std::ostringstream& builder) {
    HANDLE                   process            = GetCurrentProcess();
    HANDLE                   hThread            = GetCurrentThread();
    int                      frame_number       = 0;
    DWORD                    offset_from_symbol = 0;
    IMAGEHLP_LINE64          line               = { 0 };
    std::vector<module_data> modules;
    DWORD                    cbNeeded;
    std::vector<HMODULE>     module_handles(1);

    // Load the symbols:
    if (!SymInitialize(process, NULL, false)) {
        throw(std::logic_error("Unable to initialize symbol handler"));
    }

    DWORD symOptions = SymGetOptions();
    symOptions |= SYMOPT_LOAD_LINES | SYMOPT_UNDNAME;
    SymSetOptions(symOptions);
    EnumProcessModules(process, &module_handles[0], DWORD(module_handles.size() * sizeof(HMODULE)), &cbNeeded);
    module_handles.resize(cbNeeded / sizeof(HMODULE));
    EnumProcessModules(process, &module_handles[0], DWORD(module_handles.size() * sizeof(HMODULE)), &cbNeeded);
    std::transform(module_handles.begin(), module_handles.end(), std::back_inserter(modules), get_mod_info(process));
    void* base = modules[0].base_address;

    // Setup stuff:
    CONTEXT c      = {};
    c.ContextFlags = CONTEXT_FULL;
    RtlCaptureContext(&c);

    CONTEXT* context = &c;
#    ifdef _M_X64
    STACKFRAME64 frame     = { 0 };
    frame.AddrPC.Offset    = context->Rip;
    frame.AddrPC.Mode      = AddrModeFlat;
    frame.AddrStack.Offset = context->Rsp;
    frame.AddrStack.Mode   = AddrModeFlat;
    frame.AddrFrame.Offset = context->Rbp;
    frame.AddrFrame.Mode   = AddrModeFlat;
#    else
    STACKFRAME64 frame     = { 0 };
    frame.AddrPC.Offset    = context->Eip;
    frame.AddrPC.Mode      = AddrModeFlat;
    frame.AddrStack.Offset = context->Esp;
    frame.AddrStack.Mode   = AddrModeFlat;
    frame.AddrFrame.Offset = context->Ebp;
    frame.AddrFrame.Mode   = AddrModeFlat;
#    endif
    line.SizeOfStruct            = sizeof line;
    IMAGE_NT_HEADERS* h          = ImageNtHeader(base);
    DWORD             image_type = h->FileHeader.Machine;
    int               n          = 0;

    builder << "Stack trace: ";

    // Build the string:
    bool foundCreateException = false;
    for (int n = 0; n < 10 && frame.AddrPC.Offset != 0; ++n) {
        if (frame.AddrPC.Offset != 0) {
            std::string fnName = symbol(process, frame.AddrPC.Offset).undecorated_name();

            if (foundCreateException) {
                if (fnName == "main") {
                    break;
                } else if (fnName == "RaiseException") {
                    // This is what we get when compiled in Release mode:
                    return;
                } else if (fnName.find("HandleSehExceptionsInMethodIfSupported") != std::string::npos) {
                    // This is what you get from google test:
                    return;
                }

                builder << "  at " << fnName;
                if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &offset_from_symbol, &line)) {
                    builder << " in " << line.FileName << ":line " << line.LineNumber << std::endl;
                } else {
                    builder << std::endl;
                }

            } else if (fnName.find("AssertionFailed") != std::string::npos) {
                foundCreateException = true;
            }

        } else {
            builder << "(No Symbols)";
        }

        if (!StackWalk64(image_type, process, hThread, &frame, context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
            break;
        }
    }

    SymCleanup(process);

    return;
}

// ... till here.

std::exception CreateException(const char* condition, const char* msg) {
    static std::string container;  // Exception data _must_ be stored in a static string!
    std::ostringstream oss;
    oss << std::endl;
    oss << "Error: " << condition << " failed: " << msg << " at: " << std::endl;
    DumpStackTrace(oss);

    container = oss.str();

#    ifdef _MSC_VER
    throw std::exception(container.c_str()); /* this is usually where you want a breakpoint. */
#    else
    throw std::exception(); /* this is usually where you want a breakpoint. */
#    endif
}

#else

std::exception CreateException(const char* condition, const char* msg) {
    static std::string container;  // Exception data _must_ be stored in a static string!
    std::ostringstream oss;
    oss << std::endl;
    oss << "Error: " << condition << " failed: " << msg << " at: " << std::endl;

    container = oss.str();
    return std::exception(container.c_str()); /* this is usually where you want a breakpoint. */
}

#endif
