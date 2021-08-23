#ifdef ESP32

// Copyright 2015-2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#    include <WString.h>

#    include "esp_types.h"
#    include "esp_attr.h"
#    include "esp_err.h"
#    include "debug_helpers.h"
#    include "soc/soc_memory_layout.h"
#    include "soc/cpu.h"

static inline bool esp_stack_ptr_is_sane(uint32_t sp) {
    return !(sp < 0x3ffae010UL || sp > 0x3ffffff0UL || ((sp & 0xf) != 0));
}

static inline uint32_t esp_cpu_process_stack_pc(uint32_t pc) {
    if (pc & 0x80000000) {
        //Top two bits of a0 (return address) specify window increment. Overwrite to map to address space.
        pc = (pc & 0x3fffffff) | 0x40000000;
    }
    //Minus 3 to get PC of previous instruction (i.e. instruction executed before return address)
    return pc - 3;
}

bool IRAM_ATTR esp_backtrace_get_next_frame(esp_backtrace_frame_t* frame) {
    //Use frame(i-1)'s BS area located below frame(i)'s sp to get frame(i-1)'s sp and frame(i-2)'s pc
    void* base_save = (void*)frame->sp;  //Base save area consists of 4 words under SP
    frame->pc       = frame->next_pc;
    frame->next_pc  = *((uint32_t*)(((char*)base_save) - 16));  //If next_pc = 0, indicates frame(i-1) is the last frame on the stack
    frame->sp       = *((uint32_t*)(((char*)base_save) - 12));

    //Return true if both sp and pc of frame(i-1) are sane, false otherwise
    return (esp_stack_ptr_is_sane(frame->sp) && esp_ptr_executable((void*)esp_cpu_process_stack_pc(frame->pc)));
}

String IRAM_ATTR esp_backtrace_print(int depth) {
    char buf[80];

    //Check arguments
    if (depth <= 0) {
        return "";
    }

    //Initialize stk_frame with first frame of stack
    esp_backtrace_frame_t stk_frame;
    esp_backtrace_get_start(&(stk_frame.pc), &(stk_frame.sp), &(stk_frame.next_pc));
    //esp_cpu_get_backtrace_start(&stk_frame);
    String s = "backtrace:";
    snprintf(buf, 80, "0x%08X:0x%08X ", esp_cpu_process_stack_pc(stk_frame.pc), stk_frame.sp);
    s += buf;

    //Check if first frame is valid
    bool corrupted = (esp_stack_ptr_is_sane(stk_frame.sp) && esp_ptr_executable((void*)esp_cpu_process_stack_pc(stk_frame.pc))) ? false :
                                                                                                                                  true;

    uint32_t i = (depth <= 0) ? INT32_MAX : depth;
    while (i-- > 0 && stk_frame.next_pc != 0 && !corrupted) {
        if (!esp_backtrace_get_next_frame(&stk_frame)) {  //Get previous stack frame
            corrupted = true;
        }
        snprintf(buf, 80, "0x%08X:0x%08X ", esp_cpu_process_stack_pc(stk_frame.pc), stk_frame.sp);
        s += buf;
    }

    //Print backtrace termination marker
    if (corrupted) {
        s += " |<-CORRUPTED";
    } else if (stk_frame.next_pc != 0) {  //Backtrace continues
        s += " |<-CONTINUES";
    }
    return s;
}

#endif
