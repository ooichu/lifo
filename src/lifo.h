/*
 * Copyright (c) 2021 ooichu
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `lifo.c` for details.
 */

#ifndef LIFO_H
#define LIFO_H

#include <stdarg.h>

#define LF_VERSION "1.0"

#define LF_BLOCK_SIZE  (sizeof(void*) * 3)
#define LF_STRBUF_SIZE (sizeof(void*) * 2)
#define LF_SYM_MAX_LEN (64)

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum lf_type
{
	LF_TLST, /* list */
	LF_TSYM, /* symbol */
	LF_TSTR, /* string */
	LF_TNTV, /* native function */
	LF_TNUM, /* number */
	LF_TUSR  /* userdata */
}
lf_type;

typedef enum lf_sig
{
	LF_SOK = 0, /* signal 'ok' */
	LF_SUNFCHK, /* signal 'unfinished chunk' */
	LF_SPRSERR, /* signal 'parse error' */
	LF_SRUNERR, /* signal 'runtime error' */	
	LF_SENOMEM, /* signal 'enough memory' */
	LF_SOVRFLW, /* signal 'stack overflow' */
	LF_SUNDFLW, /* signal 'stack underflow' */
	LF_SINIERR, /* signal 'initialization error' */
	LF_SERR     /* signal 'other error' */
}
lf_sig;

typedef int lf_int;
typedef float lf_num;
typedef union lf_ref lf_ref;
typedef struct lf_obj lf_obj;
typedef struct lf_str lf_str;
typedef struct lf_chk lf_chk;
typedef struct lf_ctx lf_ctx;
typedef char (*lf_rdfn)(void* rdat);
typedef void (*lf_wrfn)(void* wdat, char c);
typedef void (*lf_ntv)(lf_ctx* ctx);
typedef void (*lf_fin)(lf_ctx* ctx, void* dat);
typedef lf_sig (*lf_hdl)(lf_ctx* ctx, lf_sig sig, const char* msg);

struct lf_str
{
	char buf[LF_STRBUF_SIZE];
	lf_str* next;
};

extern const char lf_typenames[][4];

/******************************************************************************
 * Context setup
 *****************************************************************************/

void lf_init(lf_ctx* ctx);
void lf_reset(lf_ctx* ctx);
void lf_cfg_io(lf_ctx* ctx, lf_rdfn rdfn, lf_wrfn wrfn, void* wdat);
void lf_map_mem(lf_ctx* ctx, void* mem, unsigned size);

/******************************************************************************
 * Signal handling and tracing 
 *****************************************************************************/

lf_sig lf_dfl_hdl(lf_ctx* ctx, lf_sig sig, const char* msg);
void lf_trace(lf_ctx* ctx);
void lf_raise(lf_ctx* ctx, lf_sig sig, const char* msg);
void lf_signal(lf_ctx* ctx, lf_sig sig, lf_hdl hdl);

/******************************************************************************
 * Read, evaluate 
 *****************************************************************************/

lf_sig lf_read(lf_ctx* ctx, lf_chk** chk, void* rdat);
lf_sig lf_eval(lf_ctx* ctx, const lf_chk* chk);
void lf_wipe(lf_ctx* ctx, lf_chk** chk);

/******************************************************************************
 * API 
 *****************************************************************************/

lf_obj* lf_peek(lf_ctx* ctx, lf_int i);
lf_obj* lf_take(lf_ctx* ctx, lf_int i);
lf_obj* lf_next(const lf_obj* obj);
lf_num lf_to_num(lf_ctx* ctx, const lf_obj* obj);
lf_ntv lf_to_ntv(lf_ctx* ctx, const lf_obj* obj);
lf_obj* lf_to_lst(lf_ctx* ctx, const lf_obj* obj);
void* lf_to_usr(lf_ctx* ctx, const lf_obj* obj);
const lf_str* lf_to_str(lf_ctx* ctx, const lf_obj* obj);

/******************************************************************************
 * Stack operations
 *****************************************************************************/

void lf_rol(lf_ctx* ctx);
void lf_cpy(lf_ctx* ctx);
void lf_drp(lf_ctx* ctx);
void lf_wrp(lf_ctx* ctx);
void lf_pul(lf_ctx* ctx);
void lf_apl(lf_ctx* ctx);

/******************************************************************************
 * Dictionary operations
 *****************************************************************************/

void lf_reg(lf_ctx* ctx);
void lf_rem(lf_ctx* ctx);
void lf_fnd(lf_ctx* ctx);

/******************************************************************************
 * Special operations
 *****************************************************************************/

void lf_eq(lf_ctx* ctx);
void lf_is(lf_ctx* ctx);
void lf_rf(lf_ctx* ctx);
void lf_sz(lf_ctx* ctx);

/******************************************************************************
 * Math operations
 *****************************************************************************/

void lf_add(lf_ctx* ctx);
void lf_sub(lf_ctx* ctx);
void lf_mul(lf_ctx* ctx);
void lf_div(lf_ctx* ctx);
void lf_mod(lf_ctx* ctx);
void lf_sgn(lf_ctx* ctx);

/******************************************************************************
 * Data constructors
 *****************************************************************************/

void lf_push_lst(lf_ctx* ctx);
void lf_push_sym(lf_ctx* ctx, const char* sym, unsigned len);
void lf_push_str(lf_ctx* ctx, const char* str, unsigned len);
void lf_push_ntv(lf_ctx* ctx, lf_ntv ntv);
void lf_push_num(lf_ctx* ctx, lf_num num);
void lf_push_usr(lf_ctx* ctx, void* dat, lf_fin fin);

#ifdef __cplusplus
}
#endif

#endif /* LIFO_H */

