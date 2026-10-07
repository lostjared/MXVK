package MXVKRunner;
use strict;
use warnings;
use Exporter 'import';
use File::Spec;

our @EXPORT_OK = qw(resolve_executable_path exec_command start_child stop_child);
my $is_windows = $^O eq 'MSWin32';

sub resolve_executable_path {
    my ($build_dir, $program, $exe_name) = @_;
    $exe_name .= '.exe' if $is_windows && $exe_name !~ /\.exe$/i;
    my @dirs = (File::Spec->catdir($build_dir, $program));
    if ($is_windows) {
        push @dirs, map { File::Spec->catdir($build_dir, $program, $_) }
            qw(Release RelWithDebInfo Debug MinSizeRel);
    }
    for my $dir (@dirs) {
        my $path = File::Spec->catfile($dir, $exe_name);
        return $path if -f $path && ($is_windows || -x $path);
    }
    return File::Spec->catfile($dirs[0], $exe_name);
}

sub windows_quote {
    my ($arg) = @_;
    $arg =~ s/(\\*)"/$1$1\\"/g;
    $arg =~ s/(\\+)\z/$1$1/;
    return '"' . $arg . '"';
}

sub exec_command {
    my @cmd = @_;
    if ($is_windows) {
        my $rc = system { $cmd[0] } map { windows_quote($_) } @cmd;
        die "Failed to start $cmd[0]: $!\n" if $rc == -1;
        exit($rc >> 8);
    }
    exec { $cmd[0] } @cmd;
    die "Failed to exec $cmd[0]: $!\n";
}

sub start_child {
    my @cmd = @_;
    local $ENV{MXVK_QUIET_MISSING_VALIDATION} = $ENV{MXVK_QUIET_MISSING_VALIDATION} // '1';
    if ($is_windows) {
        my $pid = system { $cmd[0] } 1, map { windows_quote($_) } @cmd;
        die "Failed to start $cmd[0]: $!\n" if $pid <= 0;
        return $pid;
    }
    my $pid = fork();
    die "Could not fork for $cmd[0]: $!\n" if !defined $pid;
    if ($pid == 0) {
        setpgrp(0, 0);
        exec_command(@cmd);
    }
    return $pid;
}

sub stop_child {
    my ($pid, $signal) = @_;
    return if !defined $pid;
    if ($is_windows) {
        kill -9, $pid;
    } else {
        kill $signal, -$pid;
        kill $signal, $pid;
    }
}

1;
