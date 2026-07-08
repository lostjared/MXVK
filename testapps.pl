#!/usr/bin/env perl
use strict;
use warnings;
use POSIX qw(:sys_wait_h);
use Time::HiRes qw(time);

my $missing_executable_exit_code = 3;
my $timeout_mode = 0;
my $timeout_seconds = 5;
my @forward_args;

for my $arg (@ARGV) {
    if ($arg eq '--timeout') {
        $timeout_mode = 1;
        next;
    }

    if ($arg =~ /^--timeout=(\d+(?:\.\d+)?)$/) {
        $timeout_mode = 1;
        $timeout_seconds = $1;
        next;
    }

    push @forward_args, $arg;
}

my @tests = qw(
    hello_world
    text_example
    sprite_example
    sprite3d_example
    static_example
    surface
    stencil
    pointsprite
    3dmath
    3dmath_cube 
    3dmath_masterpiece
    fire
    dark
    matrix
    binary_matrix
    planet
    masterpiece
    mutatris
    console_demo
    postprocess
    fractal_zoom
    model_example
    starship 
    moon
    breakout
    asteroids
    asteroids3d
    defender
    glitch_cube
    tictactoe
    pong
    pool_demo
    puzzle
    tetris
    puzzle_drop
    tux_example
    walk
    fireworks
    bluesky
);

my $count = 0;
my $skipped = 0;
my $interrupted = 0;
my $child_pid;
my @failures;

sub shell_quote {
    my ($value) = @_;
    return "''" if !defined($value) || $value eq '';
    $value =~ s/'/'"'"'/g;
    return "'$value'";
}

local $SIG{INT} = sub {
    $interrupted = 1;
    kill_child('INT');
};

sub kill_child {
    my ($signal) = @_;
    return if !defined $child_pid;
    kill $signal, -$child_pid;
    kill $signal, $child_pid;
}

for my $test (@tests) {
    last if $interrupted;

    my $test_number = $count + 1;
    my @cmd = ('./run.pl', $test, @forward_args);
    print '>> Executing: ';
    print join(' ', map { shell_quote($_) } @cmd);
    print "\n";
    my $pid = fork();
    die "Error: could not fork for program '$test': $!\n" if !defined $pid;

    if ($pid == 0) {
        setpgrp(0, 0);
        exec @cmd;
        die "Error: failed to exec program '$test': $!\n";
    }

    $child_pid = $pid;

    my $rc;
    my $timed_out = 0;
    my $deadline = time + $timeout_seconds;
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

        if ($timeout_mode && time >= $deadline) {
            $timed_out = 1;
            print ">> Timeout: closing $test after ${timeout_seconds}s\n";
            kill_child('TERM');

            my $kill_deadline = time + 2;
            while (time < $kill_deadline) {
                $wait = waitpid($pid, WNOHANG);
                if ($wait == $pid) {
                    $rc = $?;
                    last;
                }
                last if $wait == -1;
                select undef, undef, undef, 0.1;
            }

            if (!defined $rc) {
                kill_child('KILL');
                waitpid($pid, 0);
                $rc = $?;
            }
            last;
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
        my $message = "program '$test' failed to start: $!";
        if ($timeout_mode) {
            push @failures, $message;
            next;
        }
        die "Error: $message\n";
    }

    my $exit_code = $rc >> 8;
    if ($exit_code == $missing_executable_exit_code) {
        $skipped++;
        print ">> Skipping: $test was not built\n";
        next;
    }

    if ($rc & 127) {
        my $signal = $rc & 127;
        if ($timeout_mode && $timed_out) {
            $count++;
            next;
        }
        if ($signal == 2) {
            $interrupted = 1;
            last;
        }
        my $message = "program '$test' failed on test $test_number: terminated by signal $signal";
        if ($timeout_mode) {
            push @failures, $message;
            next;
        }
        die "Error: $message\n";
    }

    if ($exit_code != 0) {
        my $message = "program '$test' failed on test $test_number: exit code $exit_code";
        if ($timeout_mode) {
            push @failures, $message;
            next;
        }
        die "Error: $message\n";
    }

    $count++;
}

if ($interrupted) {
    print STDERR "Interrupted by SIGINT.\n";
    exit 130;
}

if (@failures) {
    print "$count tests ran successfully, $skipped test(s) were skipped, " . scalar(@failures) . " failure(s):\n";
    for my $failure (@failures) {
        print "  - $failure\n";
    }
    exit 1;
} elsif ($skipped) {
    print "$count tests ran successfully, $skipped test(s) were skipped.\n";
} else {
    print "$count tests ran and they were all successful.\n";
}
