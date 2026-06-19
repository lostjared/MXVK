#!/usr/bin/env perl
use strict;
use warnings;

my @tests = qw(
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
    pool_demo
    puzzle
    tetris
    tux_example
    walk
);

my $count = 0;

sub shell_quote {
    my ($value) = @_;
    return "''" if !defined($value) || $value eq '';
    $value =~ s/'/'"'"'/g;
    return "'$value'";
}

for my $test (@tests) {
    $count++;
    my @cmd = ('./run.pl', $test, @ARGV);
    print '>> Executing: ';
    print join(' ', map { shell_quote($_) } @cmd);
    print "\n";
    my $rc = system(@cmd);

    if ($rc == -1) {
        die "Error: program '$test' failed to start: $!\n";
    }

    if ($rc & 127) {
        my $signal = $rc & 127;
        die "Error: program '$test' failed on test $count: terminated by signal $signal\n";
    }

    my $exit_code = $rc >> 8;
    if ($exit_code != 0) {
        die "Error: program '$test' failed on test $count: exit code $exit_code\n";
    }
}

print "$count tests ran and they were all successful.\n";
