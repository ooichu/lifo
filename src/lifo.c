/*
 * Copyright (c) 2021 ooichu
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "lifo.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>

struct lf_obj
{
	lf_type type;
	lf_ref* ref;
	lf_obj* next;
};

#define usr(o) ((o)->ref->usr)
#define num(o) ((o)->ref->num.val)
#define ntv(o) ((o)->ref->ntv.val)
#define str(o) ((o)->ref->str.val)
#define obj(o) ((o)->ref->obj.val)

struct lf_chk
{
	lf_obj** tail; /* 'tail' of list */
	lf_obj* head;  /* 'head' of list */
	lf_chk* next;  /* next chunk */
};

union lf_ref
{
	unsigned cnt; /* count of references */
	struct { unsigned cnt; lf_obj* val; } obj;
	struct { unsigned cnt; lf_num  val; } num;
	struct { unsigned cnt; lf_ntv  val; } ntv;
	struct { unsigned cnt; lf_str* val; } str;
	struct { unsigned cnt; void* dat; lf_fin fin; } usr;
};

struct lf_ctx
{
	lf_int 	size;         /* stack size */
	lf_obj* stck;         /* stack */
	lf_obj* dict;         /* dictionary */
	lf_obj* free;         /* free stack */
	lf_obj* hold;         /* hold objects (used by lf_take) */
	lf_rdfn rdfn;         /* read function */
	lf_wrfn wrfn;         /* write function */
	void* wdat;           /* data used by write function */
	jmp_buf sbuf;         /* signal jump buffer */
	lf_hdl shdl[LF_SERR]; /* signal handlers */
};

const char lf_typenames[][4] = {"lst", "sym", "str", "ntv", "num", "usr"};

static const char* builtin_key[] =
{
	"rol", "cpy", "drp", "wrp", "pul", "apl", ";", "~", "?", "eq", "is", "rf",
	"sz", "+", "-", "*", "/", "mod", "sgn"
};

static const lf_ntv builtin_val[] =
{
	lf_rol, lf_cpy, lf_drp, lf_wrp, lf_pul, lf_apl, lf_reg, lf_rem, lf_fnd,
	lf_eq, lf_is, lf_rf, lf_sz, lf_add, lf_sub, lf_mul, lf_div, lf_mod, lf_sgn
};

static lf_ntv find_builtin(const char* str)
{
	unsigned i;
	for (i = 0; i < sizeof(builtin_key) / sizeof(builtin_key[0]); ++i)
	{
		if (strcmp(builtin_key[i], str) == 0)
		{
			return builtin_val[i];
		}
	}
	return NULL;
}

/******************************************************************************
 * Context setup
 *****************************************************************************/

void lf_init(lf_ctx* ctx)
{
	int i;
	ctx->size = 0;
	ctx->stck = NULL;
	ctx->dict = NULL;
	ctx->free = NULL;
	ctx->rdfn = NULL;
	ctx->wrfn = NULL;
	ctx->wdat = NULL;
	ctx->hold = NULL;
	for (i = 0; i < LF_SERR; ++i)
	{
		ctx->shdl[i] = lf_dfl_hdl;
	}
	lf_reset(ctx);
}

#define free_block(ctx, blk) do { \
		lf_obj* __o = (lf_obj*)(blk); \
		(__o)->next = (ctx)->free; \
		(ctx)->free = __o; \
	} while (0)

static void free_list(lf_ctx* ctx, lf_obj* obj);

static void free_ref(lf_ctx* ctx, lf_obj* obj)
{
	lf_str* str;
	if (--obj->ref->cnt == 0)
	{
		switch (obj->type)
		{
			case LF_TLST:
				free_list(ctx, obj(obj));
				break;
			case LF_TSYM:
			case LF_TSTR:
				str = str(obj);
				while (str != NULL)
				{
					lf_str* next = str->next;
					free_block(ctx, str);
					str = next;
				}
				break;
			case LF_TNTV:
			case LF_TNUM:
				break;
			case LF_TUSR:
				usr(obj).fin(ctx, usr(obj).dat);
				break;
		}
		free_block(ctx, obj->ref);
	}
}

#define free_hold(ctx) do { \
		free_list(ctx, ctx->hold); \
		ctx->hold = NULL; \
	} while (0)

static lf_obj* free_obj(lf_ctx* ctx, lf_obj* obj)
{
	lf_obj* next = obj->next;
	free_ref(ctx, obj);
	free_block(ctx, obj);
	return next;
}

static void free_list(lf_ctx* ctx, lf_obj* obj)
{
	while (obj != NULL)
	{
		obj = free_obj(ctx, obj);
	}
}

void lf_reset(lf_ctx* ctx)
{
	free_hold(ctx);
	if (setjmp(ctx->sbuf) != LF_SOK)
	{
		lf_hdl hdl = ctx->shdl[LF_SINIERR - 1];
		if (hdl(ctx, LF_SINIERR, "init error") != LF_SOK)
		{
			exit(EXIT_FAILURE);
		}
	}
}

void lf_cfg_io(lf_ctx* ctx, lf_rdfn rdfn, lf_wrfn wrfn, void* wdat)
{
	ctx->rdfn = rdfn != NULL ? rdfn : ctx->rdfn;
	ctx->wrfn = wrfn != NULL ? wrfn : ctx->wrfn;
	ctx->wdat = wdat != NULL ? wdat : ctx->wdat;
}

void lf_map_mem(lf_ctx* ctx, void* mem, unsigned size)
{
	lf_obj* obj = (lf_obj*)mem;
	while (obj < (lf_obj*)mem + size / LF_BLOCK_SIZE)
	{
		free_block(ctx, obj);
		++obj;
	}
}

/******************************************************************************
 * Signal handling and tracing
 *****************************************************************************/

static void writestr(lf_ctx* ctx, const char* str)
{
	while (*str != '\0')
	{
		ctx->wrfn(ctx->wdat, *str);
		++str;
	}
}

#define writeln(ctx, str) do { \
		writestr((ctx), str); \
		(ctx)->wrfn((ctx)->wdat, '\n'); \
	} while (0)

lf_sig lf_dfl_hdl(lf_ctx* ctx, lf_sig sig, const char* msg)
{
	char buf[32];
	sprintf(buf, "signal(%i): ", sig);
	writestr(ctx, buf);
	writeln(ctx, msg);
	return sig;
}

static void trace_obj(lf_ctx* ctx, lf_obj* obj)
{
	union
	{
		lf_str* str;
		char buf[32];
	}
	tmp;
	if (obj != NULL)
	{
next:
		switch (obj->type)
		{
			case LF_TLST:
				ctx->wrfn(ctx->wdat, '[');
				trace_obj(ctx, obj(obj));
				ctx->wrfn(ctx->wdat, ']');
				break;
			case LF_TSYM:
				for (tmp.str = str(obj); tmp.str != NULL; tmp.str = tmp.str->next)
				{
					writestr(ctx, tmp.str->buf);
				}
				break;
			case LF_TSTR:
				ctx->wrfn(ctx->wdat, '"');
				for (tmp.str = str(obj); tmp.str != NULL; tmp.str = tmp.str->next)
				{
					writestr(ctx, tmp.str->buf);
				}
				ctx->wrfn(ctx->wdat, '"');
				break;
			case LF_TNUM:
				sprintf(tmp.buf, "%.5g", num(obj));
				writestr(ctx, tmp.buf);
				break;
			case LF_TNTV:
			case LF_TUSR:
				sprintf(tmp.buf, "(%s: %p)", lf_typenames[obj->type], usr(obj).dat);
				writestr(ctx, tmp.buf);
				break;
		}
		if (obj->next)
		{
			ctx->wrfn(ctx->wdat, ' ');
			obj = obj->next;
			goto next;
		}
	}
}

void lf_trace(lf_ctx* ctx)
{
	if (ctx->stck != NULL)
	{
		trace_obj(ctx, ctx->stck);
		ctx->wrfn(ctx->wdat, '\n');
	}
	else
	{
		writeln(ctx, "-empty-");
	}
}


void lf_raise(lf_ctx* ctx, lf_sig sig, const char* msg)
{
	sig = ctx->shdl[sig - 1](ctx, sig, msg);
	if (sig != LF_SOK)
	{
		free_hold(ctx);
		longjmp(ctx->sbuf, sig);
	}
}

void lf_signal(lf_ctx* ctx, lf_sig sig, lf_hdl hdl)
{
	if (sig != LF_SOK)
	{
		ctx->shdl[sig - 1] = hdl;
	}
}

/******************************************************************************
 * Read, evaluate
 *****************************************************************************/

static void* make_block(lf_ctx* ctx)
{
	void* block;
	while (ctx->free == NULL)
	{
		lf_raise(ctx, LF_SENOMEM, "enough memory");
	}
	block = ctx->free;
	ctx->free = ctx->free->next;
	return block;
}

static lf_obj* make_obj(lf_ctx* ctx)
{
	lf_obj* obj = (lf_obj*)make_block(ctx);
	obj->ref = (lf_ref*)make_block(ctx);
	obj->ref->cnt = 1;
	return obj;
}

static lf_obj* make_ref(lf_ctx* ctx, const lf_obj* obj)
{
	lf_obj* cpy = (lf_obj*)make_block(ctx);
	cpy->type = obj->type;
	cpy->ref = obj->ref;
	++cpy->ref->cnt;
	return cpy;
}

static lf_chk* make_chk(lf_ctx* ctx, lf_chk* next)
{
	lf_chk* chk = (lf_chk*)make_block(ctx);
	chk->tail = &chk->head;
	chk->head = NULL;
	chk->next = next;
	return chk;
}

static lf_str* build_string(lf_ctx* ctx, const char* txt, unsigned len)
{
	unsigned i, n;
	lf_str* head = NULL;
	lf_str** tail = &head;
	while (len > 0)
	{
		*tail = (lf_str*)make_block(ctx);
		n = len < LF_STRBUF_SIZE - 1 ? len : LF_STRBUF_SIZE - 1;
		for (i = 0; i < n && txt[i] != '\0'; ++i)
		{
			(*tail)->buf[i] = txt[i];
		}
		(*tail)->buf[i] = '\0';
		txt += i;
		len -= i;
		(*tail)->next = NULL;
		tail = &(*tail)->next;
	}
	return head;
}

static int streq(const lf_str* a, const lf_str* b)
{
	while (a != NULL && b != NULL)
	{
		if (strcmp(a->buf, b->buf) != 0)
		{
			return 0;
		}
		a = a->next;
		b = b->next;
	}
	return a == b;
}

static int objeq(const lf_obj* a, const lf_obj* b)
{
	if (a == b) return 1;
	if (a->type == b->type)
	{
		switch (a->type)
		{
			case LF_TLST:
				a = obj(a);
				b = obj(b);
				while (a != NULL && b != NULL)
				{
					if (!objeq(a, b))
					{
						return 0;
					}
					a = a->next;
					b = b->next;
				}
				return a == b;
			case LF_TSYM:
			case LF_TSTR:
				return streq(str(a), str(b));
			case LF_TNTV:
				return ntv(a) == ntv(b);
			case LF_TNUM:
				return num(a) == num(b);
			case LF_TUSR:
				return usr(a).dat == usr(b).dat;
		}
	}
	return 0;
}

static lf_ref* search_entry(lf_chk* chk, lf_obj* obj)
{
	lf_obj* it;
	while (chk != NULL)
	{
		for (it = chk->head; it != NULL; it = it->next)
		{
			if (objeq(it, obj))
			{
				return it->ref;
			}
		}
		chk = chk->next;
	}
	return NULL;
}

static void finish_chk(lf_ctx* ctx, lf_chk** chk)
{
	lf_chk* next = (*chk)->next;
	lf_ref* ref = (lf_ref*)make_block(ctx);
	lf_obj* list = (lf_obj*)*chk;
	/* Init reference */
	ref->cnt = 1;
	ref->obj.val = (*chk)->head;
	/* Make valid list */
	list->type = LF_TLST;
	list->ref = ref;
	list->next = NULL;
	/* Bind 'list' to end of next chunk */
	*next->tail = list;
	next->tail = &list->next;
	*chk = next;
}

#define isspace(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')
#define isdelim(c) (isspace(c) || (c) ==  '[' || (c) ==  ']' || (c) ==  '"')

static void read_text(lf_ctx* ctx, lf_chk** chk, void* rdat)
{
	lf_obj* obj;
	char c = ctx->rdfn(rdat);
	union
	{
		lf_ref* ref;
		lf_ntv ntv;
		struct
		{
			union
			{
				lf_str** pstr;
				char* p;
			}
			spec;
			lf_num num;
			unsigned i;
			char buf[LF_SYM_MAX_LEN + 1];
		}
		read;
	}
	tmp;
next:
	while (isspace(c))
	{
		c = ctx->rdfn(rdat);
	}
	switch (c)
	{
		case '\0':
			return;
		case '#':
			do
			{
				c = ctx->rdfn(rdat);
			}
			while (c != '\n' && c != '\0');
			goto next;
		case '[':
			*chk = make_chk(ctx, *chk);
			c = ctx->rdfn(rdat);
			goto next;
		case ']':
			if ((*chk)->next != NULL)
			{
				finish_chk(ctx, chk);
			}
			else
			{
				lf_raise(ctx, LF_SPRSERR, "illegal list end");
			}
			c = ctx->rdfn(rdat);
			goto next;
		case '"':
			obj = make_obj(ctx);
			obj->type = LF_TSTR;
			str(obj) = NULL;
			tmp.read.spec.pstr = &str(obj);
			do
			{
				*tmp.read.spec.pstr = (lf_str*)make_block(ctx);
				for (tmp.read.i = 0; tmp.read.i < LF_STRBUF_SIZE - 1; ++tmp.read.i)
				{
					c = ctx->rdfn(rdat);
					if (c == '"')
					{
						break;
					}
					else if (c == '\0')
					{
						lf_raise(ctx, LF_SPRSERR, "unfinished string");
					}
					(*tmp.read.spec.pstr)->buf[tmp.read.i] = c;
				}
				(*tmp.read.spec.pstr)->buf[tmp.read.i] = '\0';
				(*tmp.read.spec.pstr)->next = NULL;
				tmp.read.spec.pstr = &(*tmp.read.spec.pstr)->next;
			}
			while (c != '"' && c != '\0');
			c = ctx->rdfn(rdat);
			break;
		default:
			obj = make_obj(ctx);
			for (tmp.read.i = 0; !isdelim(c) && c != '\0'; ++tmp.read.i)
			{
				if (tmp.read.i >= LF_SYM_MAX_LEN - 1)
				{
					lf_raise(ctx, LF_SPRSERR, "symbol too long");
					return;
				}
				tmp.read.buf[tmp.read.i] = c;
				c = ctx->rdfn(rdat);
			}
			tmp.read.buf[tmp.read.i] = '\0';
			tmp.ntv = find_builtin(tmp.read.buf);
			if (tmp.ntv != NULL)
			{
				obj->type = LF_TNTV;
				ntv(obj) = tmp.ntv;
			}
			else
			{
				tmp.read.num = strtod(tmp.read.buf, &tmp.read.spec.p);
				if (tmp.read.spec.p == tmp.read.buf + tmp.read.i)
				{
					obj->type = LF_TNUM;
					num(obj) = tmp.read.num;
				}
				else
				{
					obj->type = LF_TSYM;
					str(obj) = build_string(ctx, tmp.read.buf, tmp.read.i);
				}
			}
			break;
	}
	if ((tmp.ref = search_entry(*chk, obj)) != NULL)
	{
		free_ref(ctx, obj);
		obj->ref = tmp.ref;
		++tmp.ref->cnt;
	}
	obj->next = NULL;
	*(*chk)->tail = obj;
	(*chk)->tail = &obj->next;
	goto next;
} 

#undef isdelim
#undef isspace

lf_sig lf_read(lf_ctx* ctx, lf_chk** chk, void* rdat)
{
	lf_sig sig = (lf_sig)setjmp(ctx->sbuf);
	if (sig == LF_SOK)
	{
		if (*chk == NULL)
		{
			*chk = make_chk(ctx, NULL);
		}
		read_text(ctx, chk, rdat);
	}
	return sig;
}

static void unknown_symbol(lf_ctx* ctx, lf_str* str)
{
	#define err_msg "unknown symbol '"
	unsigned i;
	char buf[24 + LF_SYM_MAX_LEN] = err_msg;
	char* offset = buf + sizeof(err_msg) - 1;
	while (str != NULL)
	{
		for (i = 0; str->buf[i] != '\0'; ++i)
		{
			offset[i] = str->buf[i];
		}
		offset += i;
		str = str->next;
	}
	offset[0] = '\'';
	offset[1] = '\0';
	#undef err_msg
	lf_raise(ctx, LF_SRUNERR, buf);
}

static lf_obj* find(lf_ctx* ctx, lf_str* str)
{
	lf_obj* obj = ctx->dict;
	while (obj != NULL)
	{
		if (streq(str(obj), str))
		{
			return obj->next;
		}
		obj = obj->next->next;
	}
	unknown_symbol(ctx, str);
	return NULL;
}

static lf_obj* make_cpy(lf_ctx* ctx, const lf_obj* obj)
{
	lf_obj* cpy = NULL;
	switch (obj->type)
	{
		case LF_TLST:
		{
			lf_obj** tail;
			lf_obj* it = obj(obj);
			cpy = make_obj(ctx);
			cpy->type = obj->type;
			obj(cpy) = NULL; 
			tail = &(obj(cpy));
			while (it != NULL)
			{
				*tail = make_cpy(ctx, it);
				tail = &(*tail)->next;
				it = it->next;
			}
			break;
		}
		case LF_TSTR:
		case LF_TSYM:
		case LF_TUSR:
			cpy = make_ref(ctx, obj); 
			break;
		case LF_TNTV:
		case LF_TNUM:
			cpy = make_obj(ctx);
			cpy->type = obj->type;
			cpy->ref->usr = obj->ref->usr;
			break;
	}
	cpy->next = NULL;
	return cpy;
}

static void push_obj(lf_ctx* ctx, lf_obj* obj)
{
	obj->next = ctx->stck;
	ctx->stck = obj;
	++ctx->size; 
}

static void native_call(lf_ctx* ctx, lf_ntv fn)
{
	fn(ctx);
	free_hold(ctx);
}

static void execute(lf_ctx* ctx, lf_obj* obj)
{
begin:
	switch (obj->type)
	{
		case LF_TSYM:
			obj = find(ctx, str(obj));
			if (obj->type == LF_TLST)
			{
				obj = obj(obj);
				if (obj == NULL)
				{
					return;
				}
				while (obj->next != NULL)
				{
					execute(ctx, obj);
					obj = obj->next;
				}
			}
			goto begin;
		case LF_TNTV:
			native_call(ctx, ntv(obj));
			break;
		default:
			push_obj(ctx, make_cpy(ctx, obj));
			break;
	}
}

lf_sig lf_eval(lf_ctx* ctx, const lf_chk* chk)
{
	lf_sig sig = (lf_sig)setjmp(ctx->sbuf);
	if (sig == LF_SOK)
	{
		if (chk->next == NULL)
		{
			lf_obj* obj = chk->head;
			while (obj != NULL)
			{
				execute(ctx, obj);
				obj = obj->next;
			}
		}
		else
		{
			lf_raise(ctx, LF_SUNFCHK, "unfinished chunk");
		}
	}
	return sig;
}

void lf_wipe(lf_ctx* ctx, lf_chk** chk)
{
	while (*chk != NULL)
	{
		lf_chk* next = (*chk)->next;
		free_list(ctx, (*chk)->head);
		free_block(ctx, *chk);
		*chk = next;
	}
}

/******************************************************************************
 * API 
 *****************************************************************************/

lf_obj* lf_peek(lf_ctx* ctx, lf_int i)
{
	lf_obj* obj = ctx->stck;
	if (i >= ctx->size || ctx->size == 0)
	{
		lf_raise(ctx, LF_SUNDFLW, "stack underflow");
	}
	else if (i < 0)
	{
		lf_raise(ctx, LF_SOVRFLW, "stack overflow");
	}
	else
	{
		while (i > 0)
		{
			obj = obj->next;
			--i;
		}
	}
	return obj;
}

lf_obj* lf_take(lf_ctx* ctx, lf_int i)
{
	lf_obj* res = NULL;
	lf_obj** obj = &ctx->stck;
	if (i >= ctx->size || ctx->size == 0)
	{
		lf_raise(ctx, LF_SUNDFLW, "stack underflow");
	}
	else if (i < 0)
	{
		lf_raise(ctx, LF_SOVRFLW, "stack overflow");
	}
	else
	{
		while (i > 0)
		{
			obj = &(*obj)->next;
			--i;
		}
		res = *obj;
		*obj = res->next;
		res->next = ctx->hold;
		ctx->hold = res;
		--ctx->size;
	}
	return res;
}

lf_obj* lf_next(const lf_obj* obj)
{
	return obj != NULL ? obj->next : NULL;
}

#define check_type(require) do { \
		char buf[32]; \
		if (obj->type != require) \
		{ \
			sprintf(buf, "expected %s, got %s", \
				lf_typenames[require], lf_typenames[obj->type]); \
			lf_raise(ctx, LF_SRUNERR, buf); \
		} \
	} while (0)

lf_num lf_to_num(lf_ctx* ctx, const lf_obj* obj)
{
	check_type(LF_TNUM);
	return num(obj);
}

lf_ntv lf_to_ntv(lf_ctx* ctx, const lf_obj* obj)
{
	check_type(LF_TNTV);
	return ntv(obj);
}

void* lf_to_usr(lf_ctx* ctx, const lf_obj* obj)
{
	check_type(LF_TUSR);
	return usr(obj).dat;
}

lf_obj* lf_to_lst(lf_ctx* ctx, const lf_obj* obj)
{
	check_type(LF_TLST);
	return obj(obj);
}

const lf_str* lf_to_str(lf_ctx* ctx, const lf_obj* obj)
{
	check_type(LF_TSTR);
	return str(obj);
}

#undef check_type

/******************************************************************************
 * Generic stack operations
 *****************************************************************************/

void lf_rol(lf_ctx* ctx)
{
	lf_int step = lf_to_num(ctx, lf_take(ctx, 0));
	if (step < 0)
	{
		lf_obj* last = lf_peek(ctx, -step);
		lf_obj* first = ctx->stck;
		ctx->stck = first->next;
		first->next = last->next;
		last->next = first;
	}
	else if (step > 0)
	{
		lf_obj* prev = lf_peek(ctx, step - 1);
		lf_obj* last = prev->next;
		if (last != NULL)
		{
			prev->next = last->next;
			last->next = ctx->stck;
			ctx->stck = last;
		}
		else
		{
			lf_raise(ctx, LF_SUNDFLW, "stack underflow"); 
		}
	}
}

void lf_cpy(lf_ctx* ctx)
{
	lf_int idx = lf_to_num(ctx, lf_take(ctx, 0));
	push_obj(ctx, make_cpy(ctx, lf_peek(ctx, idx))); 
}

void lf_drp(lf_ctx* ctx)
{
	lf_take(ctx, lf_to_num(ctx, lf_take(ctx, 0)));
}

void lf_wrp(lf_ctx* ctx)
{
	lf_obj* obj;
	lf_obj* list;
	lf_int idx = (lf_int)lf_to_num(ctx, lf_take(ctx, 0));	
	obj = lf_peek(ctx, idx);
	list = make_obj(ctx);
	list->type = LF_TLST;
	obj(list) = ctx->stck;
	ctx->stck = obj->next;
	obj->next = NULL;
	push_obj(ctx, list);
	ctx->size -= idx + 1;
}

void lf_pul(lf_ctx* ctx)
{
	lf_int cnt = 0;
	lf_obj* obj = lf_take(ctx, 0);
	if (obj->type == LF_TLST)
	{
		obj = obj(obj);
		while (obj != NULL)
		{
			lf_obj* next = obj->next;
			push_obj(ctx, make_ref(ctx, obj));
			obj = next;
			++cnt;
		}
		lf_push_num(ctx, cnt);
	}
	else
	{
		char buf[32];
		sprintf(buf, "expected lst, got %s", lf_typenames[obj->type]);
		lf_raise(ctx, LF_SRUNERR, buf); 
	}
}

#define native_tail_call(ctx, fn) do { \
		if (fn == lf_apl) \
		{ \
			obj = lf_peek(ctx, 0); \
			ctx->stck = obj->next; \
			--ctx->size; \
			goto begin; \
		} \
		else if (fn == lf_eq) \
		{ \
			lf_obj* a = lf_peek(ctx, 3); \
			lf_obj* b = ctx->stck->next->next; \
			lf_obj* t = ctx->stck->next; \
			lf_obj* e = ctx->stck; \
			int res = objeq(a, b); \
			ctx->stck = free_obj(ctx, free_obj(ctx, b)); \
			ctx->size -= 4; \
			if (res) \
			{ \
				free_obj(ctx, e); \
				obj = t; \
				goto begin; \
			} \
			else \
			{ \
				free_obj(ctx, t); \
				obj = e; \
				goto begin; \
			} \
		} \
		else \
		{ \
			native_call(ctx, fn); \
		} \
	} while (0)

static void apply(lf_ctx* ctx, lf_obj* obj)
{
	lf_obj* tmp;
	lf_ntv fn;
begin:
	switch (obj->type)
	{
		case LF_TLST:
			tmp = obj(obj);
			if (--obj->ref->cnt == 0) /* list haven't references? */
			{
				free_block(ctx, obj->ref);
				free_block(ctx, obj);
				if (tmp != NULL)
				{
					/* Apply without tail return */
					while (tmp->next != NULL)
					{
						switch (tmp->type)
						{
							case LF_TSYM:
								obj = find(ctx, str(tmp));
								tmp = free_obj(ctx, tmp);
								apply(ctx, make_ref(ctx, obj));
								break;
							case LF_TNTV:
								fn = ntv(tmp);
								tmp = free_obj(ctx, tmp);
								native_call(ctx, fn);
								break;
							default:
								obj = tmp;
								tmp = tmp->next;
								push_obj(ctx, obj);
								break;
						}
					}
					/* Apply with tail return */
					switch (tmp->type)
					{
						case LF_TSYM:
							obj = find(ctx, str(tmp));
							free_obj(ctx, tmp);
							obj = make_ref(ctx, obj);
							goto begin;
						case LF_TNTV:
							fn = ntv(tmp);
							free_obj(ctx, tmp);
							native_tail_call(ctx, fn);
							break;
						default:
							obj = tmp;
							tmp = tmp->next;
							push_obj(ctx, obj);
							break;
					}
				}
			}
			else /* list not unique */
			{
				free_block(ctx, obj);
				if (tmp != NULL)
				{
					/* Apply without tail return */
					while (tmp->next != NULL)
					{
						switch (tmp->type)
						{
							case LF_TSYM:
								obj = find(ctx, str(tmp));
								tmp = tmp->next;
								apply(ctx, make_ref(ctx, obj));
								break;
							case LF_TNTV:
								fn = ntv(tmp);
								tmp = tmp->next;
								native_call(ctx, fn);
								break;
							default:
								obj = make_ref(ctx, tmp);
								tmp = tmp->next;
								push_obj(ctx, obj);
								break;
						}
					}
					/* Apply with tail return */
					switch (tmp->type)
					{
						case LF_TSYM:
							obj = find(ctx, str(tmp));
							obj = make_ref(ctx, obj);
							goto begin;
						case LF_TNTV:
							fn = ntv(tmp);
							native_tail_call(ctx, fn);
							break;
						default:
							obj = make_ref(ctx, tmp);
							tmp = tmp->next;
							push_obj(ctx, obj);
							break;
					}
				}
			}
			break;
		case LF_TSYM:
			tmp = find(ctx, str(obj));
			free_obj(ctx, obj);
			obj = make_ref(ctx, tmp);
			goto begin;
		case LF_TNTV:
			fn = ntv(obj);
			free_obj(ctx, obj);
			native_tail_call(ctx, fn);
			break;
		default:
			push_obj(ctx, obj);
			break;
	}
}

#undef native_tail_call

void lf_apl(lf_ctx* ctx)
{
	lf_obj* obj = lf_peek(ctx, 0);
	ctx->stck = obj->next;
	--ctx->size;
	apply(ctx, obj);
}

/******************************************************************************
 * Dictionary operations
 *****************************************************************************/

void lf_reg(lf_ctx* ctx)
{
	lf_obj* name = ctx->stck;
	lf_obj* value = lf_peek(ctx, 1);
	lf_to_str(ctx, name);
	ctx->stck = value->next;
	value->next = ctx->dict;
	ctx->dict = name;
	ctx->size -= 2;
}

void lf_rem(lf_ctx* ctx)
{
	lf_obj* obj = lf_take(ctx, 0);
	if (obj->type == LF_TSTR)
	{
		lf_obj** it = &ctx->dict;
		while (*it != NULL)
		{
			if (streq(str(*it), str(obj)))
			{
				*it = free_obj(ctx, free_obj(ctx, *it));
				break;
			}
			it = &(*it)->next->next;
		}
	}
	else
	{
		char buf[32];
		sprintf(buf, "expected str, got %s", lf_typenames[obj->type]);
		lf_raise(ctx, LF_SRUNERR, buf); 
	}
}

void lf_fnd(lf_ctx* ctx)
{
	lf_obj* obj = lf_take(ctx, 0);
	lf_to_str(ctx, obj);
	obj = find(ctx, str(obj));
	push_obj(ctx, make_cpy(ctx, obj));
}

/******************************************************************************
 * Special operations
 *****************************************************************************/

void lf_eq(lf_ctx* ctx)
{
	lf_obj* a = lf_peek(ctx, 3);
	lf_obj* b = ctx->stck->next->next;
	lf_obj* t = ctx->stck->next;
	lf_obj* e = ctx->stck;
	int res = objeq(a, b);
	ctx->stck = free_obj(ctx, free_obj(ctx, b));
	ctx->size -= 4;
	if (res)
	{
		free_obj(ctx, e);
		apply(ctx, t);
	}
	else
	{
		free_obj(ctx, t);
		apply(ctx, e);
	}
}

void lf_is(lf_ctx* ctx)
{
	lf_push_str(ctx, lf_typenames[lf_take(ctx, 0)->type], 3);
}

void lf_rf(lf_ctx* ctx)
{
	lf_int idx = lf_to_num(ctx, lf_take(ctx, 0));
	push_obj(ctx, make_ref(ctx, lf_peek(ctx, idx)));
}

void lf_sz(lf_ctx* ctx)
{
	lf_push_num(ctx, ctx->size);
}

/******************************************************************************
 * Math operations
 *****************************************************************************/

#define mathop(name, o) \
	void lf_##name(lf_ctx* ctx) \
	{ \
		lf_num n; \
		lf_peek(ctx, 1); \
		n = lf_to_num(ctx, ctx->stck->next) o lf_to_num(ctx, ctx->stck); \
		ctx->size -= 2; \
		ctx->stck = free_obj(ctx, free_obj(ctx, ctx->stck)); \
		lf_push_num(ctx, n); \
	}

mathop(add, +)
mathop(sub, -)
mathop(mul, *)
mathop(div, /)

#undef mathop

void lf_mod(lf_ctx* ctx)
{
	lf_num n;
	lf_peek(ctx, 1);	
	n = fmod(lf_to_num(ctx, ctx->stck->next), lf_to_num(ctx, ctx->stck));
	ctx->size -= 2;
	ctx->stck = free_obj(ctx, free_obj(ctx, ctx->stck));
	lf_push_num(ctx, n);
}

void lf_sgn(lf_ctx* ctx)
{
	lf_num n = lf_to_num(ctx, lf_take(ctx, 0));
	lf_push_num(ctx, n < 0.0 ? -1.0 : n > 0.0 ? 1.0 : 0.0);
}

/******************************************************************************
 * Data constructors
 *****************************************************************************/

void lf_push_lst(lf_ctx* ctx)
{
	lf_obj* obj = make_obj(ctx);
	obj->type = LF_TLST;
	obj(obj) = NULL;
	push_obj(ctx, obj);
}

void lf_push_sym(lf_ctx* ctx, const char* sym, unsigned len)
{
	lf_obj* obj = make_obj(ctx);
	obj->type = LF_TSYM;
	str(obj) = build_string(ctx, sym, len == 0 ? strlen(sym) : len);
	push_obj(ctx, obj);
}

void lf_push_str(lf_ctx* ctx, const char* str, unsigned len)
{
	lf_obj* obj = make_obj(ctx);
	obj->type = LF_TSTR;
	str(obj) = build_string(ctx, str, len == 0 ? strlen(str) : len);
	push_obj(ctx, obj);
}

void lf_push_ntv(lf_ctx* ctx, lf_ntv ntv)
{
	lf_obj* obj = make_obj(ctx);
	obj->type = LF_TNTV;
	ntv(obj) = ntv;
	push_obj(ctx, obj);
}

void lf_push_num(lf_ctx* ctx, lf_num num)
{
	lf_obj* obj = make_obj(ctx);
	obj->type = LF_TNUM;
	num(obj) = num;
	push_obj(ctx, obj);
}

static void no_fin(lf_ctx* ctx, void* dat)
{
	(void) ctx;
	(void) dat;
} 

void lf_push_usr(lf_ctx* ctx, void* dat, lf_fin fin)
{
	lf_obj* obj = make_obj(ctx);
	obj->type = LF_TUSR;
	usr(obj).dat = dat;
	usr(obj).fin = fin == NULL ? no_fin : fin;
	push_obj(ctx, obj);
}

/******************************************************************************
 * Standalone interpreter
 *****************************************************************************/

#ifdef LF_STANDALONE

static char readfn(void* rdat)
{
	int c = fgetc((FILE*)rdat);
	if (rdat == stdin && c == '\n')
	{
		return '\0';
	}
	return c == EOF ? '\0' : c;
}

static void writefn(void* wdat, char c)
{
	fputc(c, (FILE*)wdat);
}

static lf_sig unfchk_hdl(lf_ctx* ctx, lf_sig sig, const char* msg)
{
	(void) ctx;
	(void) msg;
	return sig;
}

static void repl(lf_ctx* ctx)
{
	lf_chk* nest;	
	lf_chk* chk = NULL;
	lf_signal(ctx, LF_SUNFCHK, unfchk_hdl);
	for (;;)
	{
		if (chk != NULL)
		{
			for (nest = chk->next; nest != NULL; nest = nest->next)
			{
				fputc('=', stdout);
			}
		}
		fputs("> ", stdout);
		if (lf_read(ctx, &chk, (void*)stdin) == LF_SOK)
		{
			switch (lf_eval(ctx, chk))
			{
				case LF_SOK:
					lf_trace(ctx);
					lf_wipe(ctx, &chk);
					break;
				case LF_SUNFCHK:
					break;
				default:
					lf_wipe(ctx, &chk);
					break;
			}
		}
		else
		{
			/* Portable fflush(stdin) */
			int c = fgetc(stdin);
			while (c != EOF && c != '\n')
			{
				c = fgetc(stdin);
			}
			lf_wipe(ctx, &chk);
		}
	}
}

static void dofile(lf_ctx* ctx, const char* filename)
{
	FILE* fp = fopen(filename, "r");
	lf_chk* chk = NULL;
	if (fp != NULL)
	{
		lf_read(ctx, &chk, fp);
		lf_eval(ctx, chk);
		lf_wipe(ctx, &chk);
		fclose(fp);
	}
	else
	{
		fprintf(stdout, "error: failed on load '%s' file!\n", filename);
	}
}

int main(int argc, char** argv)
{
	static char heap[64000];
	static lf_ctx ctx;

	printf("lifo v%s\n", LF_VERSION);

	lf_init(&ctx);
	lf_map_mem(&ctx, heap, sizeof(heap));
	lf_cfg_io(&ctx, readfn, writefn, stdout);

	dofile(&ctx, "lib.lf");

	switch (argc)
	{
		case 1:
			repl(&ctx);
			break;
		case 2:
			dofile(&ctx, argv[1]);
			lf_trace(&ctx);
			break;
		default:
			fprintf(stdout, "usage: %s [<filename>]\n", argv[0]);
			break;
	}

	return EXIT_SUCCESS;
}

#endif /* LF_STANDALONE */

