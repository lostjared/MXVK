#!/bin/bash

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec perl "$script_dir/testapps.pl" "$@"
