# Pretty-Expressive-C++
A pretty expressive formatter for C++, created to demonstrate that other Pretty-expressive implementations are "unnecarily" bottlenecked by memory.
If we want to use it for real use cases, we would hide all of the allocators in a class.

# Run Test
ulimit -s unlimited
g++ sexpr-full.cpp -O3 -o sexpr-full.out && ./sexpr-full.out
g++ concat.cpp -O3 -o concat.out && ./concat.out
g++ fill-sep.cpp -O3 -o fill-sep.out && ./fill-sep.out