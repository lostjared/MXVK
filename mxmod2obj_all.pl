#!/usr/bin/env perl
use strict;
use warnings;

use Cwd qw(abs_path);
use File::Basename qw(basename dirname);
use File::Path qw(make_path);
use File::Spec;

my $root = dirname(abs_path($0));
my $models_dir = File::Spec->catdir($root, 'models');
my $output_dir = File::Spec->catdir($models_dir, 'obj');

my $tool = $ENV{MXMOD2OBJ} // File::Spec->catfile($root, 'build', 'mxmod2obj');
if (!-x $tool) {
    $tool = 'mxmod2obj';
}

sub usage {
    die "Usage: ./mxmod2obj_all.pl\n" .
        "Converts every models/*.mxmod.z file to models/obj/*.obj using mxmod2obj.\n" .
        "Set MXMOD2OBJ to override the converter path.\n";
}

usage() if @ARGV;

if (!-d $models_dir) {
    die "Error: models directory not found at $models_dir\n";
}

if (!-x $tool) {
    die "Error: mxmod2obj not found. Build it first or set MXMOD2OBJ to the executable path.\n";
}

make_path($output_dir) if !-d $output_dir;

opendir(my $dh, $models_dir) or die "Error: cannot open $models_dir: $!\n";
my @models = sort grep { /\.mxmod\.z\z/ && -f File::Spec->catfile($models_dir, $_) } readdir($dh);
closedir($dh);

if (!@models) {
    print "No .mxmod.z files found in $models_dir\n";
    exit 0;
}

my $converted = 0;
my @failed = ();

for my $model_file (@models) {
    my $input_path = File::Spec->catfile($models_dir, $model_file);
    my $base_name = $model_file;
    $base_name =~ s/\.mxmod\.z\z//;
    my $output_base = File::Spec->catfile($output_dir, $base_name);

    print ">> Executing: $tool -i $input_path -o $output_base\n";
    if (system($tool, '-i', $input_path, '-o', $output_base) == 0) {
        $converted++;
        next;
    }

    my $exit_code = $? >> 8;
    push @failed, "$input_path (exit code $exit_code)";
    warn "Warning: conversion failed for $input_path: exit code $exit_code\n";
}

print "Converted $converted of " . scalar(@models) . " model(s) into $output_dir\n";
if (@failed) {
    print "Skipped " . scalar(@failed) . " model(s):\n";
    print "  $_\n" for @failed;
    exit 2;
}

exit 0;
