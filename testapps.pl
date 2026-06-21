#!/usr/bin/env perl
use strict;
use warnings;
use POSIX qw(:sys_wait_h);

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
    asteroids3d
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
my $interrupted = 0;
my $child_pid;

sub shell_quote {
    my ($value) = @_;
    return "''" if !defined($value) || $value eq '';
    $value =~ s/'/'"'"'/g;
    return "'$value'";
}

local $SIG{INT} = sub {
    $interrupted = 1;
    kill 'INT', $child_pid if defined $child_pid;
};

for my $test (@tests) {
    last if $interrupted;

    $count++;
    my @cmd = ('./run.pl', $test, @ARGV);
    print '>> Executing: ';
    print join(' ', map { shell_quote($_) } @cmd);
    print "\n";
    my $pid = fork();
    die "Error: could not fork for program '$test': $!\n" if !defined $pid;

    if ($pid == 0) {
        exec @cmd;
        die "Error: failed to exec program '$test': $!\n";
    }

    $child_pid = $pid;

    my $rc;
    while (1) {
        last if $interrupted;

        my $wait = waitpid($pid, WNOHANG);
        if ($wait == $pid) {
            $rc = $?; 
            last;
        }

        if ($wait == -1) {
            die "Error: failed waiting for program '$test': $!\n";
        }

        last if $interrupted;
        select undef, undef, undef, 0.1;
    }

    if ($interrupted) {
        waitpid($pid, 0);
        last;
    }

    $child_pid = undef;

    last if $interrupted;

    if ($rc == -1) {
        die "Error: program '$test' failed to start: $!\n";
    }

    if ($rc & 127) {
        my $signal = $rc & 127;
        if ($signal == 2) {
            $interrupted = 1;
            last;
        }
        die "Error: program '$test' failed on test $count: terminated by signal $signal\n";
    }

    my $exit_code = $rc >> 8;
    if ($exit_code != 0) {
        die "Error: program '$test' failed on test $count: exit code $exit_code\n";
    }
}

if ($interrupted) {
    print STDERR "Interrupted by SIGINT.\n";
    exit 130;
}

print "$count tests ran and they were all successful.\n";
