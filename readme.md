# Pretty-Expressive-C++
A pretty expressive formatter for C++, created to demonstrate that other Pretty-expressive implementations are "unnecarily" bottlenecked by memory.
If we want to use it for real use cases, we would hide all of the allocators in a class.

# Run Test
ulimit -s unlimited
g++ sexpr-full.h -o sexpr-full.out && ./sexpr-full.out