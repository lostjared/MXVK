#!/usr/bin/env perl
use strict;
use warnings;
use POSIX qw(:sys_wait_h);

my $missing_executable_exit_code = 3;

my @tests = qw(
    hello_world
    text_example
    sprite_example
    sprite3d_example
    static_example
    3dmath
    3dmath_cube 
    3dmath_masterpiece
    dark
    matrix
    binary_matrix
    planet
    masterpiece
    console_demo
    fractal_zoom
    model_example
    starship 
    moon
    asteroids
    asteroids3d
    defender
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
my $skipped = 0;
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

    my $test_number = $count + 1;
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

    my $exit_code = $rc >> 8;
    if ($exit_code == $missing_executable_exit_code) {
        $skipped++;
        print ">> Skipping: $test was not built\n";
        next;
    }

    if ($rc & 127) {
        my $signal = $rc & 127;
        if ($signal == 2) {
            $interrupted = 1;
            last;
        }
        die "Error: program '$test' failed on test $test_number: terminated by signal $signal\n";
    }

    if ($exit_code != 0) {
        die "Error: program '$test' failed on test $test_number: exit code $exit_code\n";
    }

    $count++;
}

if ($interrupted) {
    print STDERR "Interrupted by SIGINT.\n";
    exit 130;
}

if ($skipped) {
    print "$count tests ran successfully, $skipped test(s) were skipped.\n";
} else {
    print "$count tests ran and they were all successful.\n";
}
