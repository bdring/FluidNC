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
#include <stack>

#ifndef NGC_STACK_DEPTH
#    define NGC_STACK_DEPTH 10
#endif

typedef enum {
    Op_NoOp = 0,
    Op_If,
    Op_ElseIf,
    Op_Else,
    Op_EndIf,
    Op_Do,
    Op_Continue,
    Op_Break,
    Op_While,
    Op_EndWhile,
    Op_Repeat,
    Op_EndRepeat,
    Op_Return,
    Op_RaiseAlarm,
    Op_RaiseError
} ngc_cmd_t;

typedef struct {
    uint32_t    o_label;
    ngc_cmd_t   operation;
    JobSource*  file;
    size_t      file_pos;
    std::string expr;
    uint32_t    repeats;
    bool        skip;
    bool        handled;
    bool        brk;
} ngc_stack_entry_t;

std::stack<ngc_stack_entry_t> context;

std::map<std::string, ngc_cmd_t, std::less<>> commands = {
    { "IF", Op_If },
    { "ELSEIF", Op_ElseIf },
    { "ELSE", Op_Else },
    { "ENDIF", Op_EndIf },
    { "DO", Op_Do },
    { "CONTINUE", Op_Continue },
    { "BREAK", Op_Break },
    { "WHILE", Op_While },
    { "ENDWHILE", Op_EndWhile },
    { "REPEAT", Op_Repeat },
    { "ENDREPEAT", Op_EndRepeat },
    { "RETURN", Op_Return },
    { "ALARM", Op_RaiseAlarm },
    { "ERROR", Op_RaiseError },
};

static Error read_command(char* line, size_t& pos, ngc_cmd_t& operation) {
    size_t start = pos;
    while (isalpha(line[pos])) {
        ++pos;
    }
    std::string_view key(line + start, pos - start);
    auto             it = commands.find(key);
    if (it == commands.end()) {
        return Error::FlowControlSyntaxError;
    }
    operation = it->second;
    return Error::Ok;
}

static Error stack_push(uint32_t o_label, ngc_cmd_t operation, bool skip) {
    ngc_stack_entry_t ent = { o_label, operation, Job::source(), 0, "", 0, skip, false, false };
    context.push(ent);
    return Error::Ok;
}
static bool stack_pull(void) {
    if (context.empty()) {
        return false;
    }
    context.pop();
    return true;
}
void unwind_stack() {
    if (context.empty()) {
        return;
    }
    JobSource* file = context.top().file;
    while (!context.empty() && context.top().file == file) {
        stack_pull();
    }
}
void flowcontrol_init(void) {
    while (!context.empty()) {
        stack_pull();
    }
}

// Public functions

Error flowcontrol(uint32_t o_label, char* line, size_t& pos, bool& skip) {
    float     value;
    bool      skipping;
    ngc_cmd_t operation, last_op;

    Error status;

    if ((status = read_command(line, pos, operation)) != Error::Ok) {
        return status;
    }

    skipping = !context.empty() && context.top().skip;
    last_op  = context.empty() ? Op_NoOp : context.top().operation;

    switch (operation) {
        case Op_If:
            if (!skipping && (status = expression(line, pos, value)) == Error::Ok) {
                stack_push(o_label, operation, !value);
                context.top().handled = value;
            }
            break;

        case Op_ElseIf:
            if (last_op == Op_If || last_op == Op_ElseIf) {
                if (o_label == context.top().o_label && !(context.top().skip = context.top().handled) && !context.top().handled &&
                    (status = expression(line, pos, value)) == Error::Ok) {
                    if (!(context.top().skip = !value)) {
                        context.top().operation = operation;
                        context.top().handled   = true;
                    }
                }
            } else if (!skipping) {
                status = Error::FlowControlSyntaxError;
            }
            break;

        case Op_Else:
            if (last_op == Op_If || last_op == Op_ElseIf) {
                if (o_label == context.top().o_label) {
                    if (!(context.top().skip = context.top().handled)) {
                        context.top().operation = operation;
                    }
                }
            } else if (!skipping) {
                status = Error::FlowControlSyntaxError;
            }
            break;

        case Op_EndIf:
            if (last_op == Op_If || last_op == Op_ElseIf || last_op == Op_Else) {
                if (o_label == context.top().o_label) {
                    stack_pull();
                }
            } else if (!skipping) {
                status = Error::FlowControlSyntaxError;
            }
            break;

        case Op_Do:
            if (Job::active()) {
                if (!skipping) {
                    stack_push(o_label, operation, false);
                    context.top().file_pos = context.top().file->position();
                }
            } else {
                status = Error::FlowControlNotExecutingMacro;
            }
            break;

        case Op_While:
            if (Job::active()) {
                char* expr = line + pos;
                if (!context.empty() && context.top().brk) {
                    if (last_op == Op_Do && o_label == context.top().o_label) {
                        stack_pull();
                    }
                } else if (!skipping && (status = expression(line, pos, value)) == Error::Ok) {
                    if (last_op == Op_Do) {
                        if (o_label == context.top().o_label) {
                            if (value) {
                                context.top().file->set_position(context.top().file_pos);
                            } else {
                                stack_pull();
                            }
                        }
                    } else {
                        stack_push(o_label, operation, !value);
                        if (value) {
                            context.top().expr     = expr;
                            context.top().file     = Job::source();
                            context.top().file_pos = context.top().file->position();
                        }
                    }
                }
            } else {
                status = Error::FlowControlNotExecutingMacro;
            }
            break;

        case Op_EndWhile:
            if (Job::active()) {
                if (last_op == Op_While) {
                    if (!skipping && o_label == context.top().o_label) {
                        uint_fast8_t pos = 0;
                        if (!context.top().skip && (status = expression(context.top().expr.c_str(), pos, value)) == Error::Ok) {
                            if (!(context.top().skip = value == 0)) {
                                context.top().file->set_position(context.top().file_pos);
                            }
                        }
                        if (context.top().skip) {
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

        case Op_Repeat:
            if (Job::active()) {
                if (!skipping && (status = expression(line, pos, value)) == Error::Ok) {
                    // TODO - return an error if value < 0
                    // For now, just guard against negative values
                    stack_push(o_label, operation, !(value > 0.0));
                    if (value > 0.0) {
                        context.top().file     = Job::source();
                        context.top().file_pos = context.top().file->position();
                        context.top().repeats  = (uint32_t)value;
                    }
                }
            } else {
                status = Error::FlowControlNotExecutingMacro;
            }
            break;

        case Op_EndRepeat:
            if (Job::active()) {
                if (last_op == Op_Repeat) {
                    if (o_label == context.top().o_label) {
                        if (context.top().repeats && --context.top().repeats > 0.0) {
                            context.top().file->set_position(context.top().file_pos);
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

        case Op_Break:
            if (Job::active()) {
                if (!skipping) {
                    while (o_label != context.top().o_label && stack_pull())
                        ;
                    last_op = !context.empty() ? Op_NoOp : context.top().operation;
                    if (last_op == Op_Do || last_op == Op_While || last_op == Op_Repeat) {
                        if (o_label == context.top().o_label) {
                            context.top().repeats = 0;
                            context.top().brk = context.top().skip = context.top().handled = true;
                        }
                    } else {
                        status = Error::FlowControlSyntaxError;
                    }
                }
            } else {
                status = Error::FlowControlNotExecutingMacro;
            }
            break;

        case Op_Continue:
            if (Job::active()) {
                if (!skipping) {
                    while (o_label != context.top().o_label && stack_pull())
                        ;
                    if (!context.empty() && o_label == context.top().o_label) {
                        switch (context.top().operation) {
                            case Op_Repeat:
                                if (context.top().repeats && --context.top().repeats) {
                                    context.top().file->set_position(context.top().file_pos);
                                } else {
                                    stack_pull();
                                }
                                break;

                            case Op_Do:
                                context.top().file->set_position(context.top().file_pos);
                                break;

                            case Op_While: {
                                uint_fast8_t pos = 0;
                                if (!context.top().skip && (status = expression(context.top().expr.c_str(), pos, value)) == Error::Ok) {
                                    if (!(context.top().skip = value == 0)) {
                                        context.top().file->set_position(context.top().file_pos);
                                    }
                                }
                                if (context.top().skip) {
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

        case Op_RaiseAlarm:
            if (!skipping && expression(line, pos, value) == Error::Ok) {
                send_alarm((ExecAlarm)value);
            }
            break;

        case Op_RaiseError:
            if (!skipping && expression(line, pos, value) == Error::Ok) {
                status = (Error)value;
            }
            break;

        case Op_Return:
            if (Job::active()) {
#if 0
                if (!skipping && grbl.on_macro_return) {
                    unwind_stack();
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
        skip = !context.empty() && context.top().skip;
    }

    return status;
}
