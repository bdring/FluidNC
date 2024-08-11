// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Adapted from ngc_flowctrl.c in grblHAL - See https://github.com/grblHAL/core

#include <string>
#include <string.h>

#include "Protocol.h"
#include "Error.h"
#include "Expression.h"
#include "Parameters.h"
#include "Job.h"

#ifndef NGC_STACK_DEPTH
#    define NGC_STACK_DEPTH 10
#endif

typedef enum {
    NGCFlowCtrl_NoOp = 0,
    NGCFlowCtrl_If,
    NGCFlowCtrl_ElseIf,
    NGCFlowCtrl_Else,
    NGCFlowCtrl_EndIf,
    NGCFlowCtrl_Do,
    NGCFlowCtrl_Continue,
    NGCFlowCtrl_Break,
    NGCFlowCtrl_While,
    NGCFlowCtrl_EndWhile,
    NGCFlowCtrl_Repeat,
    NGCFlowCtrl_EndRepeat,
    NGCFlowCtrl_Return,
    NGCFlowCtrl_RaiseAlarm,
    NGCFlowCtrl_RaiseError
} ngc_cmd_t;

typedef struct {
    uint32_t   o_label;
    ngc_cmd_t  operation;
    JobSource* file;
    size_t     file_pos;
    char*      expr;
    uint32_t   repeats;
    bool       skip;
    bool       handled;
    bool       brk;
} ngc_stack_entry_t;

static volatile int_fast8_t stack_idx              = -1;
static ngc_stack_entry_t    stack[NGC_STACK_DEPTH] = { 0 };

static Error read_command(char* line, uint_fast8_t* pos, ngc_cmd_t* operation) {
    char  c      = line[*pos];
    Error status = Error::Ok;

    (*pos)++;

    switch (c) {
        case 'A':
            if (!strncmp(line + *pos, "LARM", 4)) {
                *operation = NGCFlowCtrl_RaiseAlarm;
                *pos += 4;
            } else
                status = Error::FlowControlSyntaxError;  // Unknown statement name starting with A
            break;

        case 'B':
            if (!strncmp(line + *pos, "REAK", 4)) {
                *operation = NGCFlowCtrl_Break;
                *pos += 4;
            } else
                status = Error::FlowControlSyntaxError;  // Unknown statement name starting with B
            break;

        case 'C':
            if (!strncmp(line + *pos, "ONTINUE", 7)) {
                *operation = NGCFlowCtrl_Continue;
                *pos += 7;
            } else
                status = Error::FlowControlSyntaxError;  // Unknown statement name starting with C
            break;

        case 'D':
            if (line[*pos] == 'O') {
                *operation = NGCFlowCtrl_Do;
                (*pos)++;
            } else
                status = Error::FlowControlSyntaxError;  // Unknown statement name starting with D
            break;

        case 'E':
            if (!strncmp(line + *pos, "LSEIF", 5)) {
                *operation = NGCFlowCtrl_ElseIf;
                *pos += 5;
            } else if (!strncmp(line + *pos, "LSE", 3)) {
                *operation = NGCFlowCtrl_Else;
                *pos += 3;
            } else if (!strncmp(line + *pos, "NDIF", 4)) {
                *operation = NGCFlowCtrl_EndIf;
                *pos += 4;
            } else if (!strncmp(line + *pos, "NDWHILE", 7)) {
                *operation = NGCFlowCtrl_EndWhile;
                *pos += 7;
            } else if (!strncmp(line + *pos, "NDREPEAT", 8)) {
                *operation = NGCFlowCtrl_EndRepeat;
                *pos += 8;
            } else if (!strncmp(line + *pos, "RROR", 4)) {
                *operation = NGCFlowCtrl_RaiseError;
                *pos += 4;
            } else
                status = Error::FlowControlSyntaxError;  // Unknown statement name starting with E
            break;

        case 'I':
            if (line[*pos] == 'F') {
                *operation = NGCFlowCtrl_If;
                (*pos)++;
            } else
                *operation = NGCFlowCtrl_RaiseError;  // Unknown statement name starting with F
            break;

        case 'R':
            if (!strncmp(line + *pos, "EPEAT", 5)) {
                *operation = NGCFlowCtrl_Repeat;
                *pos += 5;
            } else if (!strncmp(line + *pos, "ETURN", 5)) {
                *operation = NGCFlowCtrl_Return;
                *pos += 5;
            } else {
                *operation = NGCFlowCtrl_RaiseError;  // Unknown statement name starting with R
            }
            break;

        case 'W':
            if (!strncmp(line + *pos, "HILE", 4)) {
                *operation = NGCFlowCtrl_While;
                *pos += 4;
            } else {
                status = Error::FlowControlSyntaxError;  // Unknown statement name starting with W
            }
            break;

        default:
            status = Error::FlowControlSyntaxError;  // Unknown statement
    }

    return status;
}

static Error stack_push(uint32_t o_label, ngc_cmd_t operation) {
    if (stack_idx < (NGC_STACK_DEPTH - 1)) {
        stack[++stack_idx].o_label = o_label;
        stack[stack_idx].file      = Job::source();
        stack[stack_idx].operation = operation;
        return Error::Ok;
    }

    return Error::FlowControlStackOverflow;
}

static bool stack_pull(void) {
    bool ok;

    if ((ok = stack_idx >= 0)) {
        if (stack[stack_idx].expr)
            free(stack[stack_idx].expr);
        memset(&stack[stack_idx], 0, sizeof(ngc_stack_entry_t));
        stack_idx--;
    }

    return ok;
}

// Public functions

void ngc_flowctrl_unwind_stack(JobSource* file) {
    while (stack_idx >= 0 && stack[stack_idx].file == file)
        stack_pull();
}

void flowcontrol_init(void) {
    while (stack_idx >= 0) {
        stack_pull();
    }
}

Error flowcontrol(uint32_t o_label, char* line, uint_fast8_t* pos, bool& skip) {
    float     value;
    bool      skipping;
    ngc_cmd_t operation, last_op;

    Error status;

    if ((status = read_command(line, pos, &operation)) != Error::Ok)
        return status;

    skipping = stack_idx >= 0 && stack[stack_idx].skip;
    last_op  = stack_idx >= 0 ? stack[stack_idx].operation : NGCFlowCtrl_NoOp;

    switch (operation) {
        case NGCFlowCtrl_If:
            if (!skipping && (status = expression(line, pos, value)) == Error::Ok) {
                if ((status = stack_push(o_label, operation)) == Error::Ok) {
                    stack[stack_idx].skip    = value == 0.0f;
                    stack[stack_idx].handled = !stack[stack_idx].skip;
                }
            }
            break;

        case NGCFlowCtrl_ElseIf:
            if (last_op == NGCFlowCtrl_If || last_op == NGCFlowCtrl_ElseIf) {
                if (o_label == stack[stack_idx].o_label && !(stack[stack_idx].skip = stack[stack_idx].handled) &&
                    !stack[stack_idx].handled && (status = expression(line, pos, value)) == Error::Ok) {
                    if (!(stack[stack_idx].skip = value == 0.0f)) {
                        stack[stack_idx].operation = operation;
                        stack[stack_idx].handled   = true;
                    }
                }
            } else if (!skipping)
                status = Error::FlowControlSyntaxError;
            break;

        case NGCFlowCtrl_Else:
            if (last_op == NGCFlowCtrl_If || last_op == NGCFlowCtrl_ElseIf) {
                if (o_label == stack[stack_idx].o_label) {
                    if (!(stack[stack_idx].skip = stack[stack_idx].handled)) {
                        stack[stack_idx].operation = operation;
                    }
                }
            } else if (!skipping)
                status = Error::FlowControlSyntaxError;
            break;

        case NGCFlowCtrl_EndIf:
            if (last_op == NGCFlowCtrl_If || last_op == NGCFlowCtrl_ElseIf || last_op == NGCFlowCtrl_Else) {
                if (o_label == stack[stack_idx].o_label) {
                    stack_pull();
                }
            } else if (!skipping) {
                status = Error::FlowControlSyntaxError;
            }
            break;

        case NGCFlowCtrl_Do:
            if (Job::active()) {
                if (!skipping && (status = stack_push(o_label, operation)) == Error::Ok) {
                    // stack[stack_idx].file_pos = vfs_tell(hal.stream.file);
                    Job::save();
                    stack[stack_idx].skip = false;
                }
            } else {
                status = Error::FlowControlNotExecutingMacro;
            }
            break;

        case NGCFlowCtrl_While:
            if (Job::active()) {
                char* expr = line + *pos;
                if (stack_idx >= 0 && stack[stack_idx].brk) {
                    if (last_op == NGCFlowCtrl_Do && o_label == stack[stack_idx].o_label) {
                        stack_pull();
                    }
                } else if (!skipping && (status = expression(line, pos, value)) == Error::Ok) {
                    if (last_op == NGCFlowCtrl_Do) {
                        if (o_label == stack[stack_idx].o_label) {
                            if (value != 0.0f) {
                                // vfs_seek(stack[stack_idx].file, stack[stack_idx].file_pos);
                                Job::restore();
                            } else {
                                stack_pull();
                            }
                        }
                    } else if ((status = stack_push(o_label, operation)) == Error::Ok) {
                        if (!(stack[stack_idx].skip = value == 0.0f)) {
                            if ((stack[stack_idx].expr = (char*)malloc(strlen(expr) + 1))) {
                                strcpy(stack[stack_idx].expr, expr);
                                stack[stack_idx].file = Job::source();
                                // stack[stack_idx].file_pos = vfs_tell(hal.stream.file);
                                Job::save();
                            } else {
                                status = Error::FlowControlOutOfMemory;
                            }
                        }
                    }
                }
            } else {
                status = Error::FlowControlNotExecutingMacro;
            }
            break;

        case NGCFlowCtrl_EndWhile:
            if (Job::active()) {
                if (last_op == NGCFlowCtrl_While) {
                    if (!skipping && o_label == stack[stack_idx].o_label) {
                        uint_fast8_t pos = 0;
                        if (!stack[stack_idx].skip && (status = expression(stack[stack_idx].expr, &pos, value)) == Error::Ok) {
                            if (!(stack[stack_idx].skip = value == 0)) {
                                // vfs_seek(stack[stack_idx].file, stack[stack_idx].file_pos);
                                Job::restore();
                            }
                        }
                        if (stack[stack_idx].skip) {
                            stack_pull();
                        }
                    }
                } else if (!skipping) {
                    status = Error::FlowControlSyntaxError;
                }
            } else {
                status = Error::FlowControlNotExecutingMacro;
            }
            break;

        case NGCFlowCtrl_Repeat:
            if (Job::active()) {
                if (!skipping && (status = expression(line, pos, value)) == Error::Ok) {
                    if ((status = stack_push(o_label, operation)) == Error::Ok) {
                        if (!(stack[stack_idx].skip = value == 0.0f)) {
                            stack[stack_idx].file = Job::source();
                            // stack[stack_idx].file_pos = vfs_tell(hal.stream.file);
                            Job::save();
                            stack[stack_idx].repeats = (uint32_t)value;
                        }
                    }
                }
            } else {
                status = Error::FlowControlNotExecutingMacro;
            }
            break;

        case NGCFlowCtrl_EndRepeat:
            if (Job::active()) {
                if (last_op == NGCFlowCtrl_Repeat) {
                    if (!skipping && o_label == stack[stack_idx].o_label) {
                        if (stack[stack_idx].repeats && --stack[stack_idx].repeats) {
                            Job::restore();
                            // vfs_seek(stack[stack_idx].file, stack[stack_idx].file_pos);
                        } else {
                            stack_pull();
                        }
                    }
                } else if (!skipping) {
                    status = Error::FlowControlSyntaxError;
                }
            } else {
                status = Error::FlowControlNotExecutingMacro;
            }
            break;

        case NGCFlowCtrl_Break:
            if (Job::active()) {
                if (!skipping) {
                    while (o_label != stack[stack_idx].o_label && stack_pull())
                        ;
                    last_op = stack_idx >= 0 ? stack[stack_idx].operation : NGCFlowCtrl_NoOp;
                    if (last_op == NGCFlowCtrl_Do || last_op == NGCFlowCtrl_While || last_op == NGCFlowCtrl_Repeat) {
                        if (o_label == stack[stack_idx].o_label) {
                            stack[stack_idx].repeats = 0;
                            stack[stack_idx].brk = stack[stack_idx].skip = stack[stack_idx].handled = true;
                        }
                    } else {
                        status = Error::FlowControlSyntaxError;
                    }
                }
            } else {
                status = Error::FlowControlNotExecutingMacro;
            }
            break;

        case NGCFlowCtrl_Continue:
            if (Job::active()) {
                if (!skipping) {
                    while (o_label != stack[stack_idx].o_label && stack_pull())
                        ;
                    if (stack_idx >= 0 && o_label == stack[stack_idx].o_label) {
                        switch (stack[stack_idx].operation) {
                            case NGCFlowCtrl_Repeat:
                                if (stack[stack_idx].repeats && --stack[stack_idx].repeats) {
                                    Job::restore();
                                    // vfs_seek(stack[stack_idx].file, stack[stack_idx].file_pos);
                                } else {
                                    stack_pull();
                                }
                                break;

                            case NGCFlowCtrl_Do:
                                Job::restore();
                                // vfs_seek(stack[stack_idx].file, stack[stack_idx].file_pos);
                                break;

                            case NGCFlowCtrl_While: {
                                uint_fast8_t pos = 0;
                                if (!stack[stack_idx].skip && (status = expression(stack[stack_idx].expr, &pos, value)) == Error::Ok) {
                                    if (!(stack[stack_idx].skip = value == 0)) {
                                        Job::restore();
                                        // vfs_seek(stack[stack_idx].file, stack[stack_idx].file_pos);
                                    }
                                }
                                if (stack[stack_idx].skip) {
                                    if (stack[stack_idx].expr) {
                                        free(stack[stack_idx].expr);
                                        stack[stack_idx].expr = NULL;
                                    }
                                    stack_pull();
                                }
                            } break;

                            default:
                                status = Error::FlowControlSyntaxError;
                                break;
                        }
                    } else {
                        status = Error::FlowControlSyntaxError;
                    }
                }
            } else {
                status = Error::FlowControlNotExecutingMacro;
            }
            break;

        case NGCFlowCtrl_RaiseAlarm:
            if (!skipping && expression(line, pos, value) == Error::Ok) {
                send_alarm((ExecAlarm)value);
            }
            break;

        case NGCFlowCtrl_RaiseError:
            if (!skipping && expression(line, pos, value) == Error::Ok) {
                status = (Error)value;
            }
            break;

        case NGCFlowCtrl_Return:
            if (Job::active()) {
#if 0
                if (!skipping && grbl.on_macro_return) {
                    ngc_flowctrl_unwind_stack(stack[stack_idx].file);
                    if (expression(line, pos, value) == Error::Ok) {
                        set_named_param("_value", value);
                        set_named_param("_value_returned", 1.0f);
                    } else {
                        set_named_param("_value_returned", 0.0f);
                    }
                    grbl.on_macro_return();
                }
#endif
            } else {
                status = Error::FlowControlNotExecutingMacro;
            }
            break;

        default:
            status = Error::FlowControlSyntaxError;
    }

    if (status != Error::Ok) {
        flowcontrol_init();
        skip = false;
        log_debug(line);
    } else {
        skip = stack_idx >= 0 && stack[stack_idx].skip;
    }

    return status;
}
