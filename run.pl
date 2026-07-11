#!/usr/bin/env perl
use strict;
use warnings;
use Cwd 'abs_path';
use File::Basename;
use POSIX qw(:sys_wait_h);
use Time::HiRes qw(time);

my $root = dirname(abs_path($0));
my $parent = dirname($root);
my $build_dir = "$root/build/examples";
my $source_dir = "$root/examples";
my $missing_executable_exit_code = 3;

my $program = shift @ARGV;
my $timeout_mode = 0;
my $timeout_seconds = 5;

sub consume_timeout_args {
    my @args = @_;
    my @remaining;
    for (my $i = 0; $i < @args; ++$i) {
        my $arg = $args[$i];
        if ($arg eq '--timeout') {
            $timeout_mode = 1;
            if ($i + 1 < @args && $args[$i + 1] =~ /^\d+(?:\.\d+)?$/) {
                $timeout_seconds = $args[++$i];
            }
            next;
        }
        if ($arg =~ /^--timeout=(\d+(?:\.\d+)?)$/) {
            $timeout_mode = 1;
            $timeout_seconds = $1;
            next;
        }
        push @remaining, $arg;
    }
    return @remaining;
}

if (defined $program && $program =~ /^--timeout(?:=(\d+(?:\.\d+)?))?$/) {
    $timeout_mode = 1;
    $timeout_seconds = $1 if defined $1;
    if (!defined $1 && @ARGV && $ARGV[0] =~ /^\d+(?:\.\d+)?$/) {
        $timeout_seconds = shift @ARGV;
    }
    $program = shift @ARGV;
}

if (defined $program && $program eq '--all') {
    my $testapps = "$root/testapps.pl";
    if (!-f $testapps) {
        die "Error: Could not find test runner at $testapps\n";
    }

    unshift @ARGV, "--timeout=$timeout_seconds" if $timeout_mode;
    my @cmd = ($testapps, @ARGV);
    print ">> Executing: @cmd\n";
    exec(@cmd) or die "Failed to exec test runner: $!\n";
}

if (defined $program && $program eq "--debug") {
    my $rundebug = "$root/debug.pl";
    if(!-f $rundebug) {
         die "Error:  Could not find debug runner at $rundebug\n";
    }

    my @cmd = ($rundebug, @ARGV);
    exec(@cmd) or die "Failed to run debug script runner: $!\n";
}

sub resolve_executable_name {
    my ($cmake_file) = @_;
    return undef if !-f $cmake_file;
    open(my $fh, '<', $cmake_file) or return undef;
    local $/;
    my $cmake = <$fh>;
    close($fh);
    my ($target_name) = $cmake =~ /add_executable\s*\(\s*([^\s\)]+)/s;
    return undef if !$target_name;
    my $output_name;
    if ($cmake =~ /set_target_properties\s*\(\s*\Q$target_name\E\s+PROPERTIES\s+([^\)]*)\)/s) {
        my $props = $1;
        ($output_name) = $props =~ /OUTPUT_NAME\s+"([^"]+)"/s;
    }
    return $output_name // $target_name;
}

sub resolve_program_executable_path {
    my ($program_name) = @_;
    my $data_path = "$source_dir/$program_name";
    my $cmake_file = "$data_path/CMakeLists.txt";
    my $exe_name = resolve_executable_name($cmake_file);

    return undef if !$exe_name;
    return "$build_dir/$program_name/$exe_name";
}

if (!$program) {
    print "Usage: ./run.pl <program_name> [extra args...]\n";
    print "       ./run.pl <program_name> --timeout[=seconds] [extra args...]\n";
    print "       ./run.pl --timeout[=seconds] <program_name> [extra args...]\n";
    print "   or: ./run.pl --all [extra args...]\n";
    print "   or: ./run.pl --debug program [extra args...]\n\n";
    print "Available programs:\n";
    my %progs;   
    if (-d $build_dir) {
        opendir(my $dh, $build_dir);
        while (my $entry = readdir($dh)) {
            next if $entry =~ /^\./;
            next if !-d "$build_dir/$entry";

            my $exe_path = resolve_program_executable_path($entry);
            $progs{$entry} = 1 if defined $exe_path && -x $exe_path;
        }
        closedir($dh);
    }
    
    for my $p (sort keys %progs) {
        print "  $p\n";
    }

    my $counter = scalar(keys %progs);
    print "$counter total program(s)\n";
    exit 1;
}

my $program_name = basename($program);
@ARGV = consume_timeout_args(@ARGV);

if (!$timeout_mode && $ENV{CODEX_CI} && !-t STDOUT) {
    $timeout_mode = 1;
    $timeout_seconds = $ENV{MXVK_RUN_DEFAULT_TIMEOUT} // $timeout_seconds;
}

my $data_path = "$source_dir/$program_name";
my $cmake_file = "$data_path/CMakeLists.txt";

if (!-f $cmake_file) {
    die "Error: Could not find source CMake file for '$program_name' at $cmake_file\n";
}

my $exe_name = resolve_executable_name($cmake_file);
if (!$exe_name) {
    die "Error: Could not resolve executable target from $cmake_file\n";
}

my $exe_path = "$build_dir/$program_name/$exe_name";
if (-x $exe_path) {
    my $exe_dir = dirname($exe_path);
    my $resolved_exe_name = basename($exe_path);
    my $runtime_path = -d "$exe_dir/data" ? $exe_dir : $data_path;

    chdir($exe_dir) or die "Cannot cd to $exe_dir: $!\n";

    if (!-d $runtime_path) {
        warn "Warning: Data directory '$runtime_path' not found.\n";
    }

    my @cmd = ("./$resolved_exe_name", "-p", $runtime_path, @ARGV);

    print ">> Executing: @cmd\n";
    if (!$timeout_mode) {
        exec(@cmd) or die "Failed to exec $resolved_exe_name: $!\n";
    }

    my $pid = fork();
    die "Error: could not fork for program '$program_name': $!\n" if !defined $pid;
    if ($pid == 0) {
        setpgrp(0, 0);
        $ENV{MXVK_QUIET_MISSING_VALIDATION} //= '1';
        exec @cmd;
        die "Failed to exec $resolved_exe_name: $!\n";
    }

    my $rc;
    my $deadline = time + $timeout_seconds;
    while (1) {
        my $wait = waitpid($pid, WNOHANG);
        if ($wait == $pid) {
            $rc = $?;
            last;
        }
        die "Error: failed waiting for program '$program_name': $!\n" if $wait == -1;
        if (time >= $deadline) {
            print ">> Timeout reached for $program_name after ${timeout_seconds}s; closing as requested\n";
            kill 'TERM', -$pid;
            kill 'TERM', $pid;
            my $kill_deadline = time + 2;
            while (time < $kill_deadline) {
                $wait = waitpid($pid, WNOHANG);
                if ($wait == $pid) {
                    exit 0;
                }
                last if $wait == -1;
                select undef, undef, undef, 0.1;
            }
            kill 'KILL', -$pid;
            kill 'KILL', $pid;
            waitpid($pid, 0);
            exit 0;
        }
        select undef, undef, undef, 0.1;
    }

    if (($rc & 127) != 0) {
        my $signal = $rc & 127;
        die "Error: program '$program_name' terminated by signal $signal\n";
    }
    exit($rc >> 8);
} else {
    warn "Skipping '$program_name': could not find executable at $exe_path\n";
    exit $missing_executable_exit_code;
}
