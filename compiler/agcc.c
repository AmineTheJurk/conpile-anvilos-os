/*
 * agcc - AnvilOS C Compiler & ANV1 Linker
 * Portable C11 (compiles on Windows x64 via MinGW/MSVC and Linux/macOS via GCC/Clang)
 *
 * Compiles C-subset (.nc) source files into Position-Independent 32-bit x86
 * machine code wrapped in the custom AnvilOS Executable (.anv) binary format.
 *
 * Usage:
 *   agcc <input.nc> [-o <output.anv>]
 *   agcc --inspect <program.anv>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>

#include "../include/anv.h"

#define MAX_TOKENS  16384
#define MAX_CODE    65536
#define MAX_RODATA  32768
#define MAX_FUNCS   256
#define MAX_LOCALS  128
#define MAX_FIXUPS  2048
#define MAX_BREAKS  256

typedef enum {
    TOK_EOF = 0,
    TOK_IDENT,
    TOK_INT_LIT,
    TOK_STR_LIT,
    TOK_KW_INT,
    TOK_KW_VOID,
    TOK_KW_IF,
    TOK_KW_ELSE,
    TOK_KW_WHILE,
    TOK_KW_FOR,
    TOK_KW_RETURN,
    TOK_KW_BREAK,
    TOK_KW_CONTINUE,
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACE, TOK_RBRACE,
    TOK_SEMI, TOK_COMMA,
    TOK_ASSIGN, TOK_PLUS_EQ, TOK_MINUS_EQ,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_EQ, TOK_NE, TOK_LT, TOK_LE, TOK_GT, TOK_GE,
    TOK_AND, TOK_OR, TOK_BANG
} tok_kind_t;

typedef struct {
    tok_kind_t kind;
    char       text[128];
    int32_t    val;
    int        line;
} token_t;

typedef struct {
    char     name[64];
    uint32_t addr;
    bool     defined;
} func_t;

typedef struct {
    char    name[64];
    int32_t ebp_offset;
} local_t;

typedef struct {
    uint32_t imm_off;
    uint32_t pop_off;
    uint32_t str_off;
} rodata_fixup_t;

typedef struct {
    uint32_t patch_off;
    char     func_name[64];
} call_fixup_t;

typedef struct {
    uint32_t patch_off;
    int      loop_depth;
} loop_fixup_t;

static token_t  tokens[MAX_TOKENS];
static int      num_tokens = 0;
static int      tok_pos    = 0;

static uint8_t  code[MAX_CODE];
static uint32_t code_len = 0;

static uint8_t  rodata[MAX_RODATA];
static uint32_t rodata_len = 0;

static func_t   funcs[MAX_FUNCS];
static int      num_funcs = 0;

static local_t  locals[MAX_LOCALS];
static int      num_locals = 0;
static int32_t  local_stack_bytes = 0;

static rodata_fixup_t ro_fixups[MAX_FIXUPS];
static int            num_ro_fixups = 0;

static call_fixup_t   call_fixups[MAX_FIXUPS];
static int            num_call_fixups = 0;

static loop_fixup_t   break_fixups[MAX_BREAKS];
static int            num_break_fixups = 0;
static loop_fixup_t   cont_fixups[MAX_BREAKS];
static int            num_cont_fixups = 0;
static int            cur_loop_depth = 0;

static void fatal(int line, const char* msg) {
    fprintf(stderr, "agcc error (line %d): %s\n", line, msg);
    exit(1);
}

static void emit8(uint8_t b) {
    if (code_len >= MAX_CODE) fatal(tokens[tok_pos].line, "Code buffer overflow");
    code[code_len++] = b;
}

static void emit32(uint32_t v) {
    emit8((uint8_t)(v & 0xFF));
    emit8((uint8_t)((v >> 8) & 0xFF));
    emit8((uint8_t)((v >> 16) & 0xFF));
    emit8((uint8_t)((v >> 24) & 0xFF));
}

static void patch32(uint32_t off, uint32_t v) {
    code[off + 0] = (uint8_t)(v & 0xFF);
    code[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    code[off + 2] = (uint8_t)((v >> 16) & 0xFF);
    code[off + 3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint32_t add_rodata_str(const char* s, uint32_t* out_len) {
    uint32_t off = rodata_len;
    size_t len = strlen(s);
    if (rodata_len + len + 1 >= MAX_RODATA) fatal(0, "Rodata overflow");
    memcpy(rodata + rodata_len, s, len + 1);
    rodata_len += (uint32_t)(len + 1);
    if (out_len) *out_len = (uint32_t)len;
    return off;
}

/* Lexer */
static void tokenize(const char* src) {
    const char* p = src;
    int line = 1;
    num_tokens = 0;

    while (*p) {
        if (*p == '\n') { line++; p++; continue; }
        if (isspace((unsigned char)*p)) { p++; continue; }

        /* Comments */
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                if (*p == '\n') line++;
                p++;
            }
            if (*p) p += 2;
            continue;
        }
        /* Preprocessor directives (#include, #define ignored gracefully) */
        if (*p == '#') {
            while (*p && *p != '\n') p++;
            continue;
        }

        token_t* t = &tokens[num_tokens];
        memset(t, 0, sizeof(token_t));
        t->line = line;

        /* String literal */
        if (*p == '"') {
            p++;
            int i = 0;
            while (*p && *p != '"') {
                char c = *p++;
                if (c == '\\' && *p) {
                    char e = *p++;
                    if (e == 'n') c = '\n';
                    else if (e == 'r') c = '\r';
                    else if (e == 't') c = '\t';
                    else if (e == '0') c = '\0';
                    else c = e;
                }
                if (i < 126) t->text[i++] = c;
            }
            if (*p == '"') p++;
            t->text[i] = '\0';
            t->kind = TOK_STR_LIT;
            num_tokens++;
            continue;
        }

        /* Char literal */
        if (*p == '\'') {
            p++;
            int32_t c = (unsigned char)*p++;
            if (c == '\\' && *p) {
                char e = *p++;
                if (e == 'n') c = '\n';
                else if (e == 't') c = '\t';
                else if (e == '0') c = 0;
                else c = (unsigned char)e;
            }
            if (*p == '\'') p++;
            t->kind = TOK_INT_LIT;
            t->val = c;
            num_tokens++;
            continue;
        }

        /* Number */
        if (isdigit((unsigned char)*p)) {
            char* endp = NULL;
            long v = strtol(p, &endp, 0);
            t->kind = TOK_INT_LIT;
            t->val = (int32_t)v;
            p = endp;
            num_tokens++;
            continue;
        }

        /* Identifier / Keyword */
        if (isalpha((unsigned char)*p) || *p == '_') {
            int i = 0;
            while (isalnum((unsigned char)*p) || *p == '_') {
                if (i < 126) t->text[i++] = *p;
                p++;
            }
            t->text[i] = '\0';
            if      (strcmp(t->text, "int") == 0)      t->kind = TOK_KW_INT;
            else if (strcmp(t->text, "void") == 0)     t->kind = TOK_KW_VOID;
            else if (strcmp(t->text, "if") == 0)       t->kind = TOK_KW_IF;
            else if (strcmp(t->text, "else") == 0)     t->kind = TOK_KW_ELSE;
            else if (strcmp(t->text, "while") == 0)    t->kind = TOK_KW_WHILE;
            else if (strcmp(t->text, "for") == 0)      t->kind = TOK_KW_FOR;
            else if (strcmp(t->text, "return") == 0)   t->kind = TOK_KW_RETURN;
            else if (strcmp(t->text, "break") == 0)    t->kind = TOK_KW_BREAK;
            else if (strcmp(t->text, "continue") == 0) t->kind = TOK_KW_CONTINUE;
            else                                       t->kind = TOK_IDENT;
            num_tokens++;
            continue;
        }

        /* Two-char operators */
        if (p[0] == '=' && p[1] == '=') { t->kind = TOK_EQ;       p += 2; num_tokens++; continue; }
        if (p[0] == '!' && p[1] == '=') { t->kind = TOK_NE;       p += 2; num_tokens++; continue; }
        if (p[0] == '<' && p[1] == '=') { t->kind = TOK_LE;       p += 2; num_tokens++; continue; }
        if (p[0] == '>' && p[1] == '=') { t->kind = TOK_GE;       p += 2; num_tokens++; continue; }
        if (p[0] == '&' && p[1] == '&') { t->kind = TOK_AND;      p += 2; num_tokens++; continue; }
        if (p[0] == '|' && p[1] == '|') { t->kind = TOK_OR;       p += 2; num_tokens++; continue; }
        if (p[0] == '+' && p[1] == '=') { t->kind = TOK_PLUS_EQ;  p += 2; num_tokens++; continue; }
        if (p[0] == '-' && p[1] == '=') { t->kind = TOK_MINUS_EQ; p += 2; num_tokens++; continue; }

        char c = *p++;
        switch (c) {
            case '(': t->kind = TOK_LPAREN; break;
            case ')': t->kind = TOK_RPAREN; break;
            case '{': t->kind = TOK_LBRACE; break;
            case '}': t->kind = TOK_RBRACE; break;
            case ';': t->kind = TOK_SEMI;   break;
            case ',': t->kind = TOK_COMMA;  break;
            case '=': t->kind = TOK_ASSIGN; break;
            case '+': t->kind = TOK_PLUS;   break;
            case '-': t->kind = TOK_MINUS;  break;
            case '*': t->kind = TOK_STAR;   break;
            case '/': t->kind = TOK_SLASH;  break;
            case '%': t->kind = TOK_PERCENT;break;
            case '<': t->kind = TOK_LT;     break;
            case '>': t->kind = TOK_GT;     break;
            case '!': t->kind = TOK_BANG;   break;
            default: fatal(line, "Unexpected character"); break;
        }
        num_tokens++;
    }
    tokens[num_tokens].kind = TOK_EOF;
    tokens[num_tokens].line = line;
}

static token_t* peek(void) { return &tokens[tok_pos]; }
static token_t* next(void) { return &tokens[tok_pos++]; }
static bool match(tok_kind_t k) {
    if (peek()->kind == k) { tok_pos++; return true; }
    return false;
}
static void expect(tok_kind_t k, const char* msg) {
    if (!match(k)) fatal(peek()->line, msg);
}

static local_t* find_local(const char* name) {
    for (int i = 0; i < num_locals; i++) {
        if (strcmp(locals[i].name, name) == 0) return &locals[i];
    }
    return NULL;
}

static local_t* add_local(const char* name, int32_t offset) {
    if (num_locals >= MAX_LOCALS) fatal(peek()->line, "Too many local variables");
    local_t* l = &locals[num_locals++];
    strncpy(l->name, name, 63);
    l->name[63] = '\0';
    l->ebp_offset = offset;
    return l;
}

static func_t* get_or_add_func(const char* name) {
    for (int i = 0; i < num_funcs; i++) {
        if (strcmp(funcs[i].name, name) == 0) return &funcs[i];
    }
    func_t* f = &funcs[num_funcs++];
    memset(f, 0, sizeof(func_t));
    strncpy(f->name, name, 63);
    return f;
}

static void emit_load_str_eax(const char* str, uint32_t* out_strlen) {
    uint32_t str_off = add_rodata_str(str, out_strlen);
    /* call $+5 */
    emit8(0xE8); emit32(0);
    uint32_t pop_off = code_len;
    emit8(0x58); /* pop eax */
    emit8(0x05); /* add eax, imm32 */
    uint32_t imm_off = code_len;
    emit32(0);

    ro_fixups[num_ro_fixups].imm_off = imm_off;
    ro_fixups[num_ro_fixups].pop_off = pop_off;
    ro_fixups[num_ro_fixups].str_off = str_off;
    num_ro_fixups++;
}

static void emit_expr(void);
static void emit_stmt(void);

static void emit_primary(void) {
    token_t* t = peek();
    if (t->kind == TOK_INT_LIT) {
        next();
        emit8(0xB8); emit32((uint32_t)t->val); /* mov eax, imm32 */
        return;
    }
    if (t->kind == TOK_STR_LIT) {
        next();
        emit_load_str_eax(t->text, NULL);
        return;
    }
    if (t->kind == TOK_LPAREN) {
        next();
        emit_expr();
        expect(TOK_RPAREN, "Expected ')'");
        return;
    }
    if (t->kind == TOK_IDENT) {
        char name[64];
        strncpy(name, t->text, 63);
        name[63] = '\0';
        int line = t->line;
        next();

        if (match(TOK_LPAREN)) {
            /* Built-in OS Syscall Intrinsics */
            if (strcmp(name, "print") == 0 || strcmp(name, "puts") == 0) {
                if (peek()->kind == TOK_STR_LIT) {
                    token_t* st = next();
                    uint32_t slen = 0;
                    emit_load_str_eax(st->text, &slen);
                    emit8(0x89); emit8(0xC1);             /* mov ecx, eax */
                    emit8(0xBB); emit32(1);               /* mov ebx, 1 (stdout) */
                    emit8(0xBA); emit32(slen);            /* mov edx, slen */
                    emit8(0xB8); emit32(SYS_WRITE);       /* mov eax, SYS_WRITE */
                    emit8(0xCD); emit8(0x80);             /* int 0x80 */
                } else {
                    emit_expr();
                    /* ecx = eax; compute strlen in edx */
                    emit8(0x89); emit8(0xC1);             /* mov ecx, eax */
                    emit8(0x31); emit8(0xD2);             /* xor edx, edx */
                    uint32_t lp = code_len;
                    emit8(0x80); emit8(0x3C); emit8(0x11); emit8(0x00); /* cmp byte [ecx+edx], 0 */
                    emit8(0x74); emit8(0x03);             /* jz +3 */
                    emit8(0x42);                          /* inc edx */
                    emit8(0xEB); emit8((uint8_t)(int8_t)((int32_t)lp - (int32_t)(code_len + 1)));
                    emit8(0xBB); emit32(1);
                    emit8(0xB8); emit32(SYS_WRITE);
                    emit8(0xCD); emit8(0x80);
                }
                expect(TOK_RPAREN, "Expected ')' after print");
                return;
            }
            if (strcmp(name, "print_int") == 0) {
                emit_expr();
                expect(TOK_RPAREN, "Expected ')'");
                emit8(0x89); emit8(0xC3);                 /* mov ebx, eax */
                emit8(0xB8); emit32(SYS_PRINT_INT);
                emit8(0xCD); emit8(0x80);
                return;
            }
            if (strcmp(name, "putchar") == 0) {
                emit_expr();
                expect(TOK_RPAREN, "Expected ')'");
                emit8(0x89); emit8(0xC3);                 /* mov ebx, eax */
                emit8(0xB8); emit32(SYS_PUTCHAR);
                emit8(0xCD); emit8(0x80);
                return;
            }
            if (strcmp(name, "getpid") == 0) {
                expect(TOK_RPAREN, "Expected ')'");
                emit8(0xB8); emit32(SYS_GETPID);
                emit8(0xCD); emit8(0x80);
                return;
            }
            if (strcmp(name, "uptime") == 0) {
                expect(TOK_RPAREN, "Expected ')'");
                emit8(0xB8); emit32(SYS_UPTIME);
                emit8(0xCD); emit8(0x80);
                return;
            }
            if (strcmp(name, "exit") == 0) {
                emit_expr();
                expect(TOK_RPAREN, "Expected ')'");
                emit8(0x89); emit8(0xC3);
                emit8(0xB8); emit32(SYS_EXIT);
                emit8(0xCD); emit8(0x80);
                return;
            }
            if (strcmp(name, "mkdir") == 0) {
                emit_expr();
                expect(TOK_RPAREN, "Expected ')'");
                emit8(0x89); emit8(0xC3);
                emit8(0xB8); emit32(SYS_MKDIR);
                emit8(0xCD); emit8(0x80);
                return;
            }
            if (strcmp(name, "open") == 0) {
                emit_expr();
                emit8(0x50); /* push path */
                expect(TOK_COMMA, "Expected ','");
                emit_expr();
                expect(TOK_RPAREN, "Expected ')'");
                emit8(0x89); emit8(0xC1); /* mov ecx, eax (flags) */
                emit8(0x5B);              /* pop ebx (path) */
                emit8(0xB8); emit32(SYS_OPEN);
                emit8(0xCD); emit8(0x80);
                return;
            }
            if (strcmp(name, "close") == 0) {
                emit_expr();
                expect(TOK_RPAREN, "Expected ')'");
                emit8(0x89); emit8(0xC3);
                emit8(0xB8); emit32(SYS_CLOSE);
                emit8(0xCD); emit8(0x80);
                return;
            }
            if (strcmp(name, "write") == 0) {
                emit_expr(); emit8(0x50); /* fd */
                expect(TOK_COMMA, "Expected ','");
                emit_expr(); emit8(0x50); /* buf */
                expect(TOK_COMMA, "Expected ','");
                emit_expr();              /* len -> eax */
                expect(TOK_RPAREN, "Expected ')'");
                emit8(0x89); emit8(0xC2); /* mov edx, eax */
                emit8(0x59);              /* pop ecx (buf) */
                emit8(0x5B);              /* pop ebx (fd) */
                emit8(0xB8); emit32(SYS_WRITE);
                emit8(0xCD); emit8(0x80);
                return;
            }

            /* User function call: push args right-to-left */
            uint32_t arg_addrs[16];
            int argc = 0;
            /* Evaluate left-to-right into temp stack slots, or simply parse args and reorder?
               Even cleaner: evaluate left-to-right pushing each, then reverse-copy on stack? No,
               for cdecl arg0 is [esp+0], arg1 is [esp+4], so if we parse expressions we can push
               or just support up to 8 args by evaluating and reordering. Wait: if we push left-to-right
               and reverse, let's keep it clean: evaluate into stack and reverse! */
            if (peek()->kind != TOK_RPAREN) {
                do {
                    emit_expr();
                    emit8(0x50); /* push eax */
                    arg_addrs[argc++] = 0;
                } while (match(TOK_COMMA) && argc < 16);
            }
            (void)arg_addrs;
            expect(TOK_RPAREN, "Expected ')'");

            /* Reverse args on stack if argc > 1 so arg0 is at [esp+0] for cdecl */
            for (int i = 0; i < argc / 2; i++) {
                int j = argc - 1 - i;
                /* mov eax, [esp + i*4]; mov ecx, [esp + j*4]; mov [esp + i*4], ecx; mov [esp + j*4], eax */
                emit8(0x8B); emit8(0x84); emit8(0x24); emit32((uint32_t)(i * 4));
                emit8(0x8B); emit8(0x8C); emit8(0x24); emit32((uint32_t)(j * 4));
                emit8(0x89); emit8(0x8C); emit8(0x24); emit32((uint32_t)(i * 4));
                emit8(0x89); emit8(0x84); emit8(0x24); emit32((uint32_t)(j * 4));
            }

            emit8(0xE8);
            call_fixups[num_call_fixups].patch_off = code_len;
            snprintf(call_fixups[num_call_fixups].func_name,
                     sizeof(call_fixups[num_call_fixups].func_name), "%s", name);
            num_call_fixups++;
            emit32(0);

            if (argc > 0) {
                emit8(0x81); emit8(0xC4); emit32((uint32_t)(argc * 4)); /* add esp, argc*4 */
            }
            return;
        }

        /* Variable read */
        local_t* l = find_local(name);
        if (!l) fatal(line, "Undefined variable");
        emit8(0x8B); emit8(0x85); emit32((uint32_t)l->ebp_offset); /* mov eax, [ebp + off] */
        return;
    }
    fatal(t->line, "Expected expression");
}

static void emit_unary(void) {
    if (match(TOK_MINUS)) {
        emit_unary();
        emit8(0xF7); emit8(0xD8); /* neg eax */
        return;
    }
    if (match(TOK_BANG)) {
        emit_unary();
        emit8(0x85); emit8(0xC0);             /* test eax, eax */
        emit8(0x0F); emit8(0x94); emit8(0xC0);/* setz al */
        emit8(0x0F); emit8(0xB6); emit8(0xC0);/* movzx eax, al */
        return;
    }
    emit_primary();
}

static void emit_mul(void) {
    emit_unary();
    while (peek()->kind == TOK_STAR || peek()->kind == TOK_SLASH || peek()->kind == TOK_PERCENT) {
        tok_kind_t op = next()->kind;
        emit8(0x50); /* push eax */
        emit_unary();
        emit8(0x89); emit8(0xC1); /* mov ecx, eax */
        emit8(0x58);              /* pop eax */
        if (op == TOK_STAR) {
            emit8(0x0F); emit8(0xAF); emit8(0xC1); /* imul eax, ecx */
        } else {
            emit8(0x99);                           /* cdq */
            emit8(0xF7); emit8(0xF9);              /* idiv ecx */
            if (op == TOK_PERCENT) {
                emit8(0x89); emit8(0xD0);          /* mov eax, edx */
            }
        }
    }
}

static void emit_add(void) {
    emit_mul();
    while (peek()->kind == TOK_PLUS || peek()->kind == TOK_MINUS) {
        tok_kind_t op = next()->kind;
        emit8(0x50);
        emit_mul();
        emit8(0x89); emit8(0xC1);
        emit8(0x58);
        if (op == TOK_PLUS) { emit8(0x01); emit8(0xC8); } /* add eax, ecx */
        else                { emit8(0x29); emit8(0xC8); } /* sub eax, ecx */
    }
}

static void emit_rel(void) {
    emit_add();
    while (peek()->kind == TOK_LT || peek()->kind == TOK_LE ||
           peek()->kind == TOK_GT || peek()->kind == TOK_GE) {
        tok_kind_t op = next()->kind;
        emit8(0x50);
        emit_add();
        emit8(0x89); emit8(0xC1);
        emit8(0x58);
        emit8(0x39); emit8(0xC8); /* cmp eax, ecx */
        emit8(0x0F);
        if      (op == TOK_LT) emit8(0x9C); /* setl */
        else if (op == TOK_LE) emit8(0x9E); /* setle */
        else if (op == TOK_GT) emit8(0x9F); /* setg */
        else                   emit8(0x9D); /* setge */
        emit8(0xC0);
        emit8(0x0F); emit8(0xB6); emit8(0xC0);
    }
}

static void emit_eq(void) {
    emit_rel();
    while (peek()->kind == TOK_EQ || peek()->kind == TOK_NE) {
        tok_kind_t op = next()->kind;
        emit8(0x50);
        emit_rel();
        emit8(0x89); emit8(0xC1);
        emit8(0x58);
        emit8(0x39); emit8(0xC8);
        emit8(0x0F);
        emit8(op == TOK_EQ ? 0x94 : 0x95);
        emit8(0xC0);
        emit8(0x0F); emit8(0xB6); emit8(0xC0);
    }
}

static void emit_and(void) {
    emit_eq();
    while (match(TOK_AND)) {
        emit8(0x85); emit8(0xC0); /* test eax, eax */
        emit8(0x0F); emit8(0x84); uint32_t jz_false = code_len; emit32(0);
        emit_eq();
        emit8(0x85); emit8(0xC0);
        emit8(0x0F); emit8(0x95); emit8(0xC0);
        emit8(0x0F); emit8(0xB6); emit8(0xC0);
        emit8(0xE9); uint32_t jmp_end = code_len; emit32(0);
        patch32(jz_false, code_len - (jz_false + 4));
        emit8(0x31); emit8(0xC0); /* xor eax, eax */
        patch32(jmp_end, code_len - (jmp_end + 4));
    }
}

static void emit_or(void) {
    emit_and();
    while (match(TOK_OR)) {
        emit8(0x85); emit8(0xC0);
        emit8(0x0F); emit8(0x85); uint32_t jnz_true = code_len; emit32(0);
        emit_and();
        emit8(0x85); emit8(0xC0);
        emit8(0x0F); emit8(0x95); emit8(0xC0);
        emit8(0x0F); emit8(0xB6); emit8(0xC0);
        emit8(0xE9); uint32_t jmp_end = code_len; emit32(0);
        patch32(jnz_true, code_len - (jnz_true + 4));
        emit8(0xB8); emit32(1);
        patch32(jmp_end, code_len - (jmp_end + 4));
    }
}

static void emit_expr(void) {
    if (peek()->kind == TOK_IDENT &&
        (tokens[tok_pos + 1].kind == TOK_ASSIGN ||
         tokens[tok_pos + 1].kind == TOK_PLUS_EQ ||
         tokens[tok_pos + 1].kind == TOK_MINUS_EQ)) {
        token_t* id = next();
        tok_kind_t op = next()->kind;
        local_t* l = find_local(id->text);
        if (!l) fatal(id->line, "Undefined variable in assignment");
        emit_expr();
        if (op == TOK_ASSIGN) {
            emit8(0x89); emit8(0x85); emit32((uint32_t)l->ebp_offset);
        } else if (op == TOK_PLUS_EQ) {
            emit8(0x01); emit8(0x85); emit32((uint32_t)l->ebp_offset);
            emit8(0x8B); emit8(0x85); emit32((uint32_t)l->ebp_offset);
        } else {
            emit8(0x29); emit8(0x85); emit32((uint32_t)l->ebp_offset);
            emit8(0x8B); emit8(0x85); emit32((uint32_t)l->ebp_offset);
        }
        return;
    }
    emit_or();
}

static void emit_block(void) {
    expect(TOK_LBRACE, "Expected '{'");
    while (peek()->kind != TOK_RBRACE && peek()->kind != TOK_EOF) {
        emit_stmt();
    }
    expect(TOK_RBRACE, "Expected '}'");
}

static void emit_stmt(void) {
    if (peek()->kind == TOK_LBRACE) {
        emit_block();
        return;
    }
    if (match(TOK_KW_INT)) {
        token_t* id = next();
        if (id->kind != TOK_IDENT) fatal(id->line, "Expected variable name");
        local_stack_bytes += 4;
        local_t* l = add_local(id->text, -local_stack_bytes);
        if (match(TOK_ASSIGN)) {
            emit_expr();
            emit8(0x89); emit8(0x85); emit32((uint32_t)l->ebp_offset);
        } else {
            emit8(0xC7); emit8(0x85); emit32((uint32_t)l->ebp_offset); emit32(0);
        }
        expect(TOK_SEMI, "Expected ';'");
        return;
    }
    if (match(TOK_KW_RETURN)) {
        if (peek()->kind != TOK_SEMI) emit_expr();
        else { emit8(0x31); emit8(0xC0); }
        expect(TOK_SEMI, "Expected ';'");
        emit8(0x89); emit8(0xEC); /* mov esp, ebp */
        emit8(0x5D);              /* pop ebp */
        emit8(0xC3);              /* ret */
        return;
    }
    if (match(TOK_KW_IF)) {
        expect(TOK_LPAREN, "Expected '('");
        emit_expr();
        expect(TOK_RPAREN, "Expected ')'");
        emit8(0x85); emit8(0xC0); /* test eax, eax */
        emit8(0x0F); emit8(0x84); uint32_t jz_else = code_len; emit32(0);
        emit_stmt();
        if (match(TOK_KW_ELSE)) {
            emit8(0xE9); uint32_t jmp_end = code_len; emit32(0);
            patch32(jz_else, code_len - (jz_else + 4));
            emit_stmt();
            patch32(jmp_end, code_len - (jmp_end + 4));
        } else {
            patch32(jz_else, code_len - (jz_else + 4));
        }
        return;
    }
    if (match(TOK_KW_WHILE)) {
        expect(TOK_LPAREN, "Expected '('");
        uint32_t loop_top = code_len;
        emit_expr();
        expect(TOK_RPAREN, "Expected ')'");
        emit8(0x85); emit8(0xC0);
        emit8(0x0F); emit8(0x84); uint32_t jz_exit = code_len; emit32(0);

        cur_loop_depth++;
        emit_stmt();
        for (int i = 0; i < num_cont_fixups; i++) {
            if (cont_fixups[i].loop_depth == cur_loop_depth) {
                patch32(cont_fixups[i].patch_off, loop_top - (cont_fixups[i].patch_off + 4));
                cont_fixups[i].loop_depth = -1;
            }
        }
        emit8(0xE9); emit32(loop_top - (code_len + 4));
        patch32(jz_exit, code_len - (jz_exit + 4));
        for (int i = 0; i < num_break_fixups; i++) {
            if (break_fixups[i].loop_depth == cur_loop_depth) {
                patch32(break_fixups[i].patch_off, code_len - (break_fixups[i].patch_off + 4));
                break_fixups[i].loop_depth = -1;
            }
        }
        cur_loop_depth--;
        return;
    }
    if (match(TOK_KW_FOR)) {
        expect(TOK_LPAREN, "Expected '('");
        if (peek()->kind != TOK_SEMI) {
            if (match(TOK_KW_INT)) {
                token_t* id = next();
                local_stack_bytes += 4;
                local_t* l = add_local(id->text, -local_stack_bytes);
                if (match(TOK_ASSIGN)) {
                    emit_expr();
                    emit8(0x89); emit8(0x85); emit32((uint32_t)l->ebp_offset);
                }
            } else {
                emit_expr();
            }
        }
        expect(TOK_SEMI, "Expected ';'");
        uint32_t cond_top = code_len;
        uint32_t jz_exit = 0;
        bool has_cond = (peek()->kind != TOK_SEMI);
        if (has_cond) {
            emit_expr();
            emit8(0x85); emit8(0xC0);
            emit8(0x0F); emit8(0x84); jz_exit = code_len; emit32(0);
        }
        expect(TOK_SEMI, "Expected ';'");

        /* Jump over step to body */
        emit8(0xE9); uint32_t jmp_body = code_len; emit32(0);
        uint32_t step_top = code_len;
        if (peek()->kind != TOK_RPAREN) emit_expr();
        expect(TOK_RPAREN, "Expected ')'");
        emit8(0xE9); emit32(cond_top - (code_len + 4));

        patch32(jmp_body, code_len - (jmp_body + 4));
        cur_loop_depth++;
        emit_stmt();
        for (int i = 0; i < num_cont_fixups; i++) {
            if (cont_fixups[i].loop_depth == cur_loop_depth) {
                patch32(cont_fixups[i].patch_off, step_top - (cont_fixups[i].patch_off + 4));
                cont_fixups[i].loop_depth = -1;
            }
        }
        emit8(0xE9); emit32(step_top - (code_len + 4));
        if (has_cond) patch32(jz_exit, code_len - (jz_exit + 4));
        for (int i = 0; i < num_break_fixups; i++) {
            if (break_fixups[i].loop_depth == cur_loop_depth) {
                patch32(break_fixups[i].patch_off, code_len - (break_fixups[i].patch_off + 4));
                break_fixups[i].loop_depth = -1;
            }
        }
        cur_loop_depth--;
        return;
    }
    if (match(TOK_KW_BREAK)) {
        expect(TOK_SEMI, "Expected ';'");
        emit8(0xE9);
        break_fixups[num_break_fixups].patch_off = code_len;
        break_fixups[num_break_fixups].loop_depth = cur_loop_depth;
        num_break_fixups++;
        emit32(0);
        return;
    }
    if (match(TOK_KW_CONTINUE)) {
        expect(TOK_SEMI, "Expected ';'");
        emit8(0xE9);
        cont_fixups[num_cont_fixups].patch_off = code_len;
        cont_fixups[num_cont_fixups].loop_depth = cur_loop_depth;
        num_cont_fixups++;
        emit32(0);
        return;
    }
    if (match(TOK_SEMI)) return;

    emit_expr();
    expect(TOK_SEMI, "Expected ';'");
}

static void parse_program(void) {
    /* Entry stub at offset 0: jmp main */
    emit8(0xE9);
    call_fixups[num_call_fixups].patch_off = code_len;
    strcpy(call_fixups[num_call_fixups].func_name, "main");
    num_call_fixups++;
    emit32(0);

    while (peek()->kind != TOK_EOF) {
        if (!match(TOK_KW_INT) && !match(TOK_KW_VOID)) {
            fatal(peek()->line, "Expected function return type ('int' or 'void')");
        }
        token_t* fn = next();
        if (fn->kind != TOK_IDENT) fatal(fn->line, "Expected function name");
        func_t* f = get_or_add_func(fn->text);
        f->addr = code_len;
        f->defined = true;

        num_locals = 0;
        local_stack_bytes = 0;

        expect(TOK_LPAREN, "Expected '('");
        int param_idx = 0;
        if (peek()->kind != TOK_RPAREN) {
            do {
                expect(TOK_KW_INT, "Expected 'int' parameter type");
                token_t* pn = next();
                if (pn->kind != TOK_IDENT) fatal(pn->line, "Expected parameter name");
                add_local(pn->text, 8 + param_idx * 4);
                param_idx++;
            } while (match(TOK_COMMA));
        }
        expect(TOK_RPAREN, "Expected ')'");

        /* Function prologue */
        emit8(0x55);                           /* push ebp */
        emit8(0x89); emit8(0xE5);              /* mov ebp, esp */
        emit8(0x81); emit8(0xEC);              /* sub esp, imm32 */
        uint32_t stack_patch = code_len;
        emit32(0);

        emit_block();

        /* Implicit return 0 at end of function */
        emit8(0x31); emit8(0xC0);              /* xor eax, eax */
        emit8(0x89); emit8(0xEC);              /* mov esp, ebp */
        emit8(0x5D);                           /* pop ebp */
        emit8(0xC3);                           /* ret */

        patch32(stack_patch, (uint32_t)local_stack_bytes);
    }

    /* Patch function calls & entry jump */
    for (int i = 0; i < num_call_fixups; i++) {
        func_t* target = NULL;
        for (int j = 0; j < num_funcs; j++) {
            if (strcmp(funcs[j].name, call_fixups[i].func_name) == 0 && funcs[j].defined) {
                target = &funcs[j];
                break;
            }
        }
        if (!target) {
            fprintf(stderr, "agcc link error: undefined function '%s'\n", call_fixups[i].func_name);
            exit(1);
        }
        int32_t rel = (int32_t)target->addr - (int32_t)(call_fixups[i].patch_off + 4);
        patch32(call_fixups[i].patch_off, (uint32_t)rel);
    }

    /* Patch PIC rodata offsets now that code_len is finalized */
    for (int i = 0; i < num_ro_fixups; i++) {
        int32_t delta = (int32_t)(code_len + ro_fixups[i].str_off) - (int32_t)ro_fixups[i].pop_off;
        patch32(ro_fixups[i].imm_off, (uint32_t)delta);
    }
}

static int inspect_anv(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { perror("fopen"); return 1; }
    anv_header_t hdr;
    if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        fprintf(stderr, "Invalid/truncated ANV file\n");
        fclose(f);
        return 1;
    }
    fclose(f);
    printf("ANV1 Inspector: %s\n", path);
    printf("  Magic:       0x%08X (%s)\n", hdr.magic, hdr.magic == ANV_MAGIC ? "VALID ANV1" : "INVALID");
    printf("  Version:     %u\n", hdr.version);
    printf("  Arch:        %u (%s)\n", hdr.arch, hdr.arch == ANV_ARCH_X86_32 ? "x86-32" : "unknown");
    printf("  App Name:    %s\n", hdr.app_name);
    printf("  Entry Off:   0x%08X\n", hdr.entry_off);
    printf("  Code Size:   %u bytes\n", hdr.code_size);
    printf("  Rodata Size: %u bytes\n", hdr.rodata_size);
    printf("  BSS Size:    %u bytes\n", hdr.bss_size);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "AnvilOS C Compiler (agcc) v1.0 - Targets AnvilOS .anv\n");
        fprintf(stderr, "Usage: %s <input.nc> [-o <output.anv>]\n", argv[0]);
        fprintf(stderr, "       %s --inspect <program.anv>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--inspect") == 0) {
        if (argc < 3) return 1;
        return inspect_anv(argv[2]);
    }

    const char* in_path = argv[1];
    char out_path[260] = "a.anv";
    if (argc >= 4 && strcmp(argv[2], "-o") == 0) {
        strncpy(out_path, argv[3], sizeof(out_path) - 1);
    } else {
        strncpy(out_path, in_path, sizeof(out_path) - 5);
        char* dot = strrchr(out_path, '.');
        if (dot) strcpy(dot, ".anv");
        else strcat(out_path, ".anv");
    }

    FILE* fp = fopen(in_path, "rb");
    if (!fp) { perror("open input"); return 1; }
    fseek(fp, 0, SEEK_END);
    long fsz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* src = (char*)malloc((size_t)fsz + 1);
    if (!src) return 1;
    size_t n = fread(src, 1, (size_t)fsz, fp);
    src[n] = '\0';
    fclose(fp);

    tokenize(src);
    parse_program();
    free(src);

    /* Extract basename for app_name */
    const char* base = strrchr(out_path, '/');
    const char* b2   = strrchr(out_path, '\\');
    if (b2 && (!base || b2 > base)) base = b2;
    base = base ? base + 1 : out_path;

    anv_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic       = ANV_MAGIC;
    hdr.version     = ANV_VERSION;
    hdr.arch        = ANV_ARCH_X86_32;
    hdr.flags       = ANV_FLAG_CONSOLE;
    hdr.entry_off   = 0;
    hdr.code_size   = code_len;
    hdr.rodata_size = rodata_len;
    hdr.bss_size    = 0;
    hdr.stack_size  = 4096;
    snprintf(hdr.app_name, sizeof(hdr.app_name), "%s", base);

    FILE* out = fopen(out_path, "wb");
    if (!out) { perror("open output"); return 1; }
    fwrite(&hdr, 1, sizeof(hdr), out);
    if (code_len > 0)   fwrite(code, 1, code_len, out);
    if (rodata_len > 0) fwrite(rodata, 1, rodata_len, out);
    fclose(out);

    printf("[agcc] Compiled %s -> %s (code=%uB, rodata=%uB, ANV1)\n",
           in_path, out_path, code_len, rodata_len);
    return 0;
}
