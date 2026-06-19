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
child_pid=""
interrupted=0

on_int() {
    interrupted=1
    if [ -n "$child_pid" ]; then
        kill -INT "$child_pid" 2>/dev/null
    fi
}

trap on_int INT

for test in "${tests[@]}"; do
    if [ "$interrupted" -ne 0 ]; then
        break
    fi

    count=$((count + 1))
    printf '>> Executing: ./run.pl %q' "$test"
    for arg in "$@"; do
        printf ' %q' "$arg"
    done
    printf '\n'
    ./run.pl "$test" "$@" &
    child_pid=$!
    wait "$child_pid"
    rc=$?
    child_pid=""

    if [ "$interrupted" -ne 0 ]; then
        break
    fi

    if [ "$rc" -ne 0 ]; then
        echo "Program '$test' failed on test $count: exit code $rc" >&2
        exit "$rc"
    fi
done

trap - INT

if [ "$interrupted" -ne 0 ]; then
    echo "Interrupted by SIGINT." >&2
    exit 130
fi

echo "$count tests ran and they were all successful."
