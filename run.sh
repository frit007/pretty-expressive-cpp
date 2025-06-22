#!/usr/bin/env sh

# Get the base name of the file without the .ml extension
exe=${1%}

# Apply specific string mappings
case "$exe" in
  concat) exe="concat" ;;
  fill_sep) exe="fill-sep" ;;
  flatten) exe="flatten" ;;
  json) exe="json" ;;  
  sexp_full) exe="sexpr-full" ;;
  sexp_random) exe="sexpr-random" ;;
esac

# Shift positional parameters to exclude the first argument
shift

ulimit -s unlimited

# Run the command with the transformed exe name
"./$exe.out" "$@"