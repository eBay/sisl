#ifndef __STACK_TRACE_H
#define __STACK_TRACE_H

#include "logging.h"
#include <unwind.h>
#include <dlfcn.h>    // for dladdr
#include <cxxabi.h>   // for __cxa_demangle
#include <execinfo.h> // for backtrace
#include <cstdio>
#include <sstream>
#include <iostream>
#include <cstring>

struct trace_arg {
    int cnt = 0;
};

//--------------- taken from libc --------------
#define _BACKTRACE_FORMAT "%-4d%-35s 0x%016lx %s + %lu\n"
#define _BACKTRACE_FORMAT_SIZE 84 /* %lu can take up to 20, does not include %s, includes NUL */
#define _BACKTRACE_ADDRESS_LEN 18 /* 0x + 16 (no NUL) */
void dump_frame_info(int frame, const void* addr, const Dl_info* info) {
    char symbuf[_BACKTRACE_ADDRESS_LEN + 1];
    const char* image = "???";
    const char* symbol = "0x0";
    uintptr_t symbol_offset = 0;
    char demangled_name[2048];

    if (info->dli_fname) {
        const char* tmp = std::strrchr(info->dli_fname, '/');
        if (tmp == NULL)
            image = info->dli_fname;
        else
            image = tmp + 1;
    }

    if (info->dli_sname) {
        symbol = info->dli_sname;
        symbol_offset = (uintptr_t)addr - (uintptr_t)info->dli_saddr;
    } else if (info->dli_fname) {
        symbol = image;
        symbol_offset = (uintptr_t)addr - (uintptr_t)info->dli_fbase;
    } else if (0 < snprintf(symbuf, sizeof(symbuf), "0x%lx", (uintptr_t)info->dli_saddr)) {
        symbol = symbuf;
        symbol_offset = (uintptr_t)addr - (uintptr_t)info->dli_saddr;
    } else {
        symbol_offset = (uintptr_t)addr;
    }

    const char* proc_name = symbol;
    std::printf("Trying to demangle %s\n", symbol);
    if (symbol[0] == '_') {
        int status;
        size_t len = sizeof(demangled_name);
        if ((proc_name = abi::__cxa_demangle(symbol, demangled_name, &len, &status)) == 0) {
            proc_name = symbol;
        } else {
            std::printf("Symbol %s demangling failed\n", proc_name);
        }
    }

    std::printf(_BACKTRACE_FORMAT, frame, image, (uintptr_t)addr, proc_name, symbol_offset);
}
//----------------------------------------------

_Unwind_Reason_Code UnwindCB(struct _Unwind_Context* unwind_context, void* cont) {
    trace_arg* arg = static_cast<trace_arg*>(cont);
    int ipBefore = 0;
    uintptr_t ip = _Unwind_GetIPInfo(unwind_context, &ipBefore);
    //    if (!ipBefore)
    //        --ip;
    //---------------------------------
    Dl_info info;
    //        typedef struct dl_info {
    //            const char      *dli_fname;     /* Pathname of shared object */
    //            void            *dli_fbase;     /* Base address of shared object */
    //            const char      *dli_sname;     /* Name of nearest symbol */
    //            void            *dli_saddr;     /* Address of nearest symbol */
    //        } Dl_info;
    //char buff[2048] = "unavailable";

    // https://code.woboq.org/llvm/compiler-rt/lib/sanitizer_common/sanitizer_stacktrace_printer.h.html
    // Here's the full list of available placeholders:
    //    33    //   %% - represents a '%' character;
    //    34    //   %n - frame number (copy of frame_no);
    //    35    //   %p - PC in hex format;
    //    36    //   %m - path to module (binary or shared object);
    //    37    //   %o - offset in the module in hex format;
    //    38    //   %f - function name;
    //    39    //   %q - offset in the function in hex format (*if available*);
    //    40    //   %s - path to source file;
    //    41    //   %l - line in the source file;
    //    42    //   %c - column in the source file;
    //    43    //   %F - if function is known to be <foo>, prints "in <foo>", possibly
    //    44    //        followed by the offset in this function, but only if source file
    //    45    //        is unknown;
    //    46    //   %S - prints file/line/column information;
    //    47    //   %L - prints location information: file/line/column, if it is known, or
    //    48    //        module+offset if it is known, or (<unknown module>) string.
    //    49    //   %M - prints module basename and offset, if it is known, or PC.

    if constexpr (0) {
        //printf("%-3d ", ++arg->cnt);
        //__sanitizer_symbolize_pc((void*)ip, "%F at %L", buff, sizeof(buff));
        //puts(buff);
    } else {
        auto const* addr = reinterpret_cast<const void*>(ip);
        if (dladdr(addr, &info)) {
            dump_frame_info(++arg->cnt, addr, &info);
        }
    }

    return _URC_NO_REASON;
}

void backtrace_unwind() {
    if constexpr (0) {
        //__sanitizer_print_stack_trace();
    } else {
        // printBaseAddrInfo();
        LOGCRITICAL("Dumping stack trace using unwind:");
        //      (*__safestack_preinit)();

        trace_arg arg;
        _Unwind_Backtrace(UnwindCB, &arg);
    }
}
#endif
