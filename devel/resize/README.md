The pauliebits resize function and growth pattern
===============================================

Running `python resize.py` will display the pauliebits growth pattern.
This is done by appending one bit to a pauliebits in a loop, and displaying
the allocated size of the pauliebits object each time it changes.

The program `resize.c` contains a distilled version of the `resize()`
function which contains the implementation of this growth pattern.
Running this C program gives exactly the same output.
