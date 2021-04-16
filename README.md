# lifo
A embeddable stack-based programming language implemented in ANSI C.

    # push parity string for numbers from 0 to 10 
    0 [dup 10 <=]
    [
    	dup 2 mod 0 = "Even" "Odd" if
    	swp
    	++
    ]
    loop
