# С API

## Interpreter context
**Lifo** is implemented reentrant. The `lf_ctx` structure is used to store the state of the interpreter. It contains:
1. Count of element in the stack;
2. Woriking stack;
3. Free objects list;
4. Dictionary;
5. Signal handlers;
6. Signal jump buffer.

The context must be initialized before use.

    lf_ctx ctx;
    lf_init(&ctx);

## Configure IO
**Lifo** has no operating system dependencies, so to be able to display messages and read input, you need to set up `lf_rdfn` and `lf_wrfn` callbacks.
`lf_rdfn`  should return the next character from the file or buffer like getс.
`lf_wrfn` should place a character in a file or buffer.
To configure IO you should call function `lf_cfg_io`.

    /* Read code from file */
    char readch(void* rdat)
    {
    	int c = fgetc((FILE*)rdat);
    	return c == EOF ? '\0' : c;
    }
    /* Write output to stdout */
    char writech(void* wdat, char c)
    {
    	fputc(c, (FILE*)wdat);
    }
    /* Configure IO */
    lf_cfg_io(&ctx, readch, writech, stdout);

## Signals
Signals are like exceptions in many modern languages. There is a fixed number of signals that can be assigned their own `lf_hdl` handler (with the exception of the `LF_SOK` signal). Signal list:
1. `LF_SOK` -- no errors;
2. `LF_SUNFCHK` -- unfinished chuk (caused when list isn't closed);
3. `LF_SPRSERR` -- error while parsing the code;
4. `LF_SRUNERR` -- error on runtime;
5. `LF_SMEMOUT` -- memory out;
6. `LF_SOVRFLW` -- stack overflow (trying access element by negative index); 
7. `LF_SUNDFLW` -- stack underflow (trying access element by invalid index);
8. `LF_SERR`-- reserved for other errors.

Signal can be *raised* using `lf_raise` function. When the signal is *raised*, its handler is called, which returns the resulting singal. If the resulting singal is `LF_SOK`, then the execution will continue, otherwise the execution will be interrupted.

## Memory managment
**Lifo** does not independently allocate or free memory using functions such as `malloc`, `free`. Memory for use by the interpreter must be allocated after initializing the context using the `lf_map_mem` function.
You can "feed" the interpreter several chunks of memory that are not related to each other at any time.

## Objects
The `object` represents the code and data of the program. An `object` can have several basic types: list, symbol, string, native function, number and userdata; for more details check language reference.

## Read and evaluation
To execute a script, you must first read it using the `lf_read` function. The read code is stored in the objects of the `lf_chk` structure. To execute the readed code, you need to call the `lf_eval` function.

    lf_chk* chk = NULL;
    FILE* fp = fopen("script.lf", "r");
    /* Try read */
    if (lf_read(&ctx, &chk, (void*)fp) != LF_SOK)
    {
    	/* Fail on read code */
    }
    /*  Try eval */
    if (lf_eval(&ctx, chk) != LF_SOK)
    {
    	/* Fail on eval code */
    }
    fclose(fp);

## Checking type and getting data
Use the `lf_peek` function to check the stack size and get a specific object. `lf_take` works the same as `lf_peek` except that `lf_take` pops an item off the stack. To get the data of an object, use the functions `lf_to_num`, `lf_to_ntv`, `lf_to_usr`, `lf_to_lst` and `lf_to_str`. Use `lf_next` to iterate over objects.

## Strings
Because of the way the memory manager is implemented, strings are stored as a list of string buffers, reperesented by structure `lf_str`, each of which is `LF_STRBUF_SIZE` long and null-terminated. String not allow escape sequences.

## Userdata
User data is just a pointer without a type (`void*`). You can assign a finalizer (`lf_fin`) to the custom data if needed.

    /* Just wrap on free() */
    void finalizer(lf_ctx* ctx, void* dat)
    {
    	(void) ctx;
    	free(dat);
    }
    /* Push userdata with finalizer*/
    lf_push_usr(&ctx, malloc(128), finalizer);
    /* Push userdata without finalizer */
    lf_push_usr(&ctx, (int[]){1, 2, 3}, NULL);

## Creating native functions
You can extend the capabilities of the language with **C** functions. All native functions must be of type `lf_ntv`. For example, function that prints string (without parsing escape-sequences):

    /* Prototype: ... "string" print */
    void print_ntv(lf_ctx* ctx)
    {
    	/* Get string data */
    	lf_str* str = lf_to_str(ctx, lf_peek(ctx, 0));
    	/* Iterate each string buffer and print it */
    	while (str != NULL)
    	{
    		fputs(str->buf, stdout);
    		str = str->next;
    	}
    	fputc('\n', stdout);
    	/* Remove string from stack */
    	lf_push_num(ctx, 0);
    	lf_drp(ctx);
    }

