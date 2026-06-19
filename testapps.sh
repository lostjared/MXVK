#!/bin/bash

tests=(
    hello_world
    text_example
    sprite_example
    sprite3d_example
    static_example
    matrix
    binary_matrix
    masterpiece
    console_demo
    fractal_zoom
    model_example
    starship
    planet
    asteroids
    glitch_cube
    tictactoe
    pong
    tetris
    pool_demo
    puzzle
    tux_example
    walk
)

count=0

for test in "${tests[@]}"; do
    count=$((count + 1))
    printf '>> Executing: ./run.pl %q' "$test"
    for arg in "$@"; do
        printf ' %q' "$arg"
    done
    printf '\n'
    ./run.pl "$test" "$@"
    rc=$?

    if [ "$rc" -ne 0 ]; then
        echo "Program '$test' failed on test $count: exit code $rc" >&2
        exit "$rc"
    fi
done

echo "$count tests ran and they were all successful."
