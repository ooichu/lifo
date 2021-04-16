
# Language
**Lifo** is a stack-based programming language. All data is stored on the interpreter stack. Stack items can be *moved*, *removed*, and *copied* within the stack. The stack depth can vary depending on the amount of memory allocated to the interpreter.
## Data types
### List
Mnemonic - `lst`.  Beginning and end of the list are indicated by square brackets.
Example:

    [1 a "Hello"]

### Symbol
Mnemonic - `sym`. A symbol is a string separated by blank characters (spaces, tabs, newline, etc.) and cannot be considered a number. The maximum symbol length is 64 (depends on the build of the language) characters.
Example:

    just-a-symbol 1imsymbol +

### String
Mnemonic - `str`. A string is a sequence of any characters starting with double quotes (`"`) and ending with them. Escape sequences not parse.
Example:

    "Hello, world!"

### Native function
Mnemonic - `ntv`. A native function is a **C** function that can be called from a **Lifo**. This data type can only be added via the **C API**.
### Number
Mnemonic - `num`. A number in **Lifo** is a floating point number. It can be represented in decimal, hexadecimal and scientific format.
### User data
Mnemonic - `usr`. User data can only be added through the **C API**.

## Syntax
**Lifo** has a very primitive syntax. Lexemes can be any printable character (except reserved ones) and must be separated from other tokens using blank characters (spaces, newlines, tabs). The beginning and end of the list are indicated by square brackets `[`, `]`. The beginning of a single line comment is indicated by the character `#`. The beginning and end of a line is indicated by a symbol `"`. Characters `[`, `]`, `#` and `"` are reserved and cannot be used as part of other tokens.

## Stack operations
### rol (mnemonic - `rol`)
    ... n rol
Cyclic shift of first `n+1` elements.
### cpy (mnemonic - `cpy`)
    ... i cpy
Copy element, indexed with `i`.
### drp (mnemonic - `drp`)
    ... i drp
Delete element, indexed with `i`.
### wrp (mnemonic - `wrp`)
    ... i drp
Create a list from the first `n+1` elements.
### pul (mnemonic - `pul`)
    ... [...] pul
Pops all elements from the list in reverse order and pops the number of elements in the list.
### apl (mnemonic - `apl`)
    ... <anything> apl
*Applies* the first element.
Rules of *application*:
 1. **lst**: each item in the list will be *executed*;
 2. **sym**: *applies* value of symbol;
 3. **str**: nothing;
 4. **ntv**: calls native function;
 5. **num**: nothing;
 6. **usr**: nothing.

Rules of *execution*:
 1. **lst**: nothing;
 2. **sym**: *applies* value of symbol;
 3. **str**: nothing;
 4. **ntv**: calls native function;
 5. **num**: nothing;
 6. **usr**: nothing.

## Dictionary operations
### reg (mnemonic - `;`)
    ... <anything> "name-of-symbol" ;
Register a character in the dictionary. The top-most element must be a string. Note that if you register a symbol like `"symbol with spaces",` you cannot refer to it as `symbol with spaces`.
### rem (mnemonic - `~`)
    ... "symbol-name" ~
Removes a character from the dictionary. In case the symbol is not in the dictionary, it does nothing. This way you can remove the symbol declared as `"symbol with spaces"`. Doesn't check for occurrence of the same symbol in the dictionary. If a symbol with the same name already exists, then it will not be available until the current symbol with the same name is removed.
### fnd (mnemonic - `?`)
    ... "symbol-name" ?
Returns the first `symbol-name` value. This way you can refer to the symbol declared as `"symbol with spaces"`. If the value of the symbol is not in the dictionary, then the signal `LF_TUNKSYM` is raised.
## Special operations
### eq (mnemonic - `eq`)

    ... <anything> <anything> [<if-equals>] [<if-not-equals>] eq
Checks the 3rd and 4th elements of the stack for equality, in case of equality, applies the 2nd element, otherwise - the 1st.
### is (mnemonic - `is`)
    ... <anything> is
Pushes typename of top element.
### rf (mnemonic - `rf`)
    ... <anything> i rf
Pushes reference of element, indexed with `i`. 
### sz (mnemonic - `sz`)
    ... sz
Pushes number of elements in stack.
## Math operations
### add (mnemonic - `+`)
    ... <number> <number> +
Pushes sum of two top numbers.
### sub (mnemonic - `-`)
    ... <number> <number> -
Pushes difference of two top numbers.
### mul (mnemonic - `*`)
    ... <number> <number> *
Pushes product of two top numbers.
### div (mnemonic - `/`)
    ... <number> <number> /
Pushes quotient of two top numbers.
### mod (mnemonic - `mod`)
    ... <number> <number> mod
Pushes modulo of two top numbers.
### sgn (mnemonic - `sgn`)
    ... <number> sgn
Pushes sign of top number.

