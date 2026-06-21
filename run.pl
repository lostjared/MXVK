#!/usr/bin/env perl
use strict;
use warnings;
use Cwd 'abs_path';
use File::Basename;

my $root = dirname(abs_path($0));
my $parent = dirname($root);
my $build_dir = "$root/build/examples";
my $source_dir = "$root/examples";

my $program = shift @ARGV;

if (defined $program && $program eq '--all') {
    my $testapps = "$root/testapps.pl";
    if (!-f $testapps) {
        die "Error: Could not find test runner at $testapps\n";
    }

    my @cmd = ($testapps, @ARGV);
    print ">> Executing: @cmd\n";
    exec(@cmd) or die "Failed to exec test runner: $!\n";
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

sub should_use_build_asset_path {
    my ($cmake_file) = @_;
    return 0 if !-f $cmake_file;

    open(my $fh, '<', $cmake_file) or return 0;
    local $/;
    my $cmake = <$fh>;
    close($fh);

    return $cmake =~ /ASSET_DIR\s*=\s*"\$<TARGET_FILE_DIR:[^>]+>"/s
        || $cmake =~ /ASSET_DIR\s*=\s*"\$\{CMAKE_CURRENT_BINARY_DIR\}"/s;
}

if (!$program) {
    print "Usage: ./run.pl <program_name> [extra args...]\n";
    print "   or: ./run.pl --all [extra args...]\n\n";
    print "Available programs:\n";
    my %progs;   
    if (-d $build_dir) {
        opendir(my $dh, $build_dir);
        while (my $entry = readdir($dh)) {
            next if $entry =~ /^\./;
            $progs{$entry} = 1 if -d "$build_dir/$entry";
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
my $use_build_asset_path = should_use_build_asset_path($cmake_file);

if (-x $exe_path) {
    my $exe_dir = dirname($exe_path);
    my $resolved_exe_name = basename($exe_path);
    my $runtime_path = $use_build_asset_path ? $exe_dir : $data_path;

    chdir($exe_dir) or die "Cannot cd to $exe_dir: $!\n";

    if (!-d $runtime_path) {
        warn "Warning: Data directory '$runtime_path' not found.\n";
    }

    my @cmd = ("./$resolved_exe_name", "-p", $runtime_path, @ARGV);
    
    print ">> Executing: @cmd\n";
    exec(@cmd) or die "Failed to exec $resolved_exe_name: $!\n";
} else {
    die "Error: Could not find executable for '$program_name' at $exe_path\n";
}
