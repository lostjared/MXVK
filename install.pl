#!/usr/bin/env perl

use strict;
use warnings;
use Cwd qw(abs_path getcwd);
use File::Basename qw(dirname);
use File::Spec;
use FindBin qw($RealBin);
use Getopt::Long qw(GetOptionsFromArray :config pass_through no_auto_abbrev);
use IO::Select;
use IPC::Open3;
use Symbol qw(gensym);

$| = 1;

my $source_dir = abs_path($RealBin);
my $build_dir = File::Spec->catdir($source_dir, "build");
my $prefix;
my $jobs = default_jobs();
my $fresh = 0;
my $install = 1;
my $use_sudo;
my $help = 0;
my ($debug_mode, $validation, $cv, $jpeg, $examples, $fractal_zoom, $python_module, $moltenvk);
my ($with_cuda, $with_eigen, $with_mxwrite, $with_mixer) = ("AUTO", "AUTO", "AUTO", "AUTO");
my ($python_install_dir, $glslc_executable);
my ($mutatris_desktop, $asteroids3d_desktop, $asteroids_net_desktop);
my ($configuration, $component, $strip_install);

GetOptionsFromArray(
    \@ARGV,
    "build-dir|B=s" => \$build_dir,
    "prefix=s" => \$prefix,
    "jobs|j=i" => \$jobs,
    "fresh" => \$fresh,
    "install!" => \$install,
    "sudo!" => \$use_sudo,
    "debug!" => \$debug_mode,
    "validation!" => \$validation,
    "cv!" => \$cv,
    "cuda=s" => \$with_cuda,
    "eigen=s" => \$with_eigen,
    "mxwrite=s" => \$with_mxwrite,
    "mixer=s" => \$with_mixer,
    "jpeg!" => \$jpeg,
    "examples!" => \$examples,
    "fractal-zoom!" => \$fractal_zoom,
    "python-module!" => \$python_module,
    "python-install-dir=s" => \$python_install_dir,
    "moltenvk!" => \$moltenvk,
    "mutatris-desktop!" => \$mutatris_desktop,
    "asteroids3d-desktop!" => \$asteroids3d_desktop,
    "asteroids-net-desktop!" => \$asteroids_net_desktop,
    "glslc=s" => \$glslc_executable,
    "config=s" => \$configuration,
    "component=s" => \$component,
    "strip" => \$strip_install,
    "help|h" => \$help,
) or usage(2);

usage(0) if $help;
fail("--jobs must be a positive integer") if $jobs < 1;
for my $option (["--cuda", \$with_cuda], ["--eigen", \$with_eigen], ["--mxwrite", \$with_mxwrite], ["--mixer", \$with_mixer]) {
    next unless defined ${ $option->[1] };
    fail("$option->[0] must be AUTO, ON, or OFF") unless ${ $option->[1] } =~ /^(?:AUTO|ON|OFF)$/i;
    ${ $option->[1] } = uc(${ $option->[1] });
}

$build_dir = File::Spec->rel2abs($build_dir, getcwd());

print "MXVK configure, build, and install\n";
print "  Source: $source_dir\n";
print "  Build:  $build_dir\n";
print "  Jobs:   $jobs\n";
print "  Install: " . ($install ? "yes" : "no") . "\n\n";

my @missing_tools;
push @missing_tools, "cmake" unless command_path("cmake");

my $compiler_is_configured = grep {
    /^-DCMAKE_(?:CXX_COMPILER|TOOLCHAIN_FILE)(?::[^=]+)?=/
} @ARGV;
if (!$compiler_is_configured && !$ENV{CXX}) {
    push @missing_tools, "a C++ compiler (c++, g++, or clang++)"
        unless command_path("c++") || command_path("g++") || command_path("clang++");
}

my $glslc_is_configured = defined $glslc_executable || grep { /^-DGLSLC_EXECUTABLE(?::[^=]+)?=/ } @ARGV;
push @missing_tools, "glslc (the Vulkan shader compiler)"
    if !$glslc_is_configured && !command_path("glslc");

if (@missing_tools) {
    print STDERR "ERROR: Missing required build tools:\n";
    print STDERR "  - $_\n" for @missing_tools;
    print_dependency_help(join("\n", @missing_tools));
    exit 2;
}

my ($cmake_version_output, $cmake_version_status) = capture_command("cmake", "--version");
if ($cmake_version_status != 0 || $cmake_version_output !~ /cmake version\s+(\d+)\.(\d+)/i) {
    fail("Unable to determine the installed CMake version.");
}
my ($cmake_major, $cmake_minor) = ($1, $2);
if ($cmake_major < 3 || ($cmake_major == 3 && $cmake_minor < 10)) {
    fail("CMake 3.10 or newer is required; found $cmake_major.$cmake_minor.");
}

my @configure_command = ("cmake");
if ($fresh) {
    if ($cmake_major > 3 || ($cmake_major == 3 && $cmake_minor >= 24)) {
        push @configure_command, "--fresh";
    } else {
        fail("--fresh requires CMake 3.24 or newer. Remove '$build_dir' manually or upgrade CMake.");
    }
}

my $generator_is_configured = grep { /^-G(?:$|.)|^--generator(?:=|$)/ } @ARGV;
my $cached_generator = $fresh ? undef : read_cache_value($build_dir, "CMAKE_GENERATOR");
if ($generator_is_configured) {
    print "CMake generator: selected by command-line option.\n";
} elsif (defined $cached_generator && length $cached_generator) {
    print "CMake generator: $cached_generator (from existing build directory).\n";
} elsif (command_path("ninja")) {
    push @configure_command, "-G", "Ninja";
    print "CMake generator: Ninja (detected automatically).\n";
} else {
    print "CMake generator: CMake default (ninja was not found).\n";
}

push @configure_command, "-S", $source_dir, "-B", $build_dir;
push @configure_command, "-DCMAKE_INSTALL_PREFIX=$prefix" if defined $prefix;
append_cmake_bool(\@configure_command, "DEBUG_MODE", $debug_mode);
append_cmake_bool(\@configure_command, "VALIDATION", $validation);
append_cmake_bool(\@configure_command, "CV", $cv);
append_cmake_value(\@configure_command, "WITH_CUDA", $with_cuda);
append_cmake_value(\@configure_command, "WITH_EIGEN", $with_eigen);
append_cmake_value(\@configure_command, "WITH_MXWRITE", $with_mxwrite);
append_cmake_value(\@configure_command, "WITH_MIXER", $with_mixer);
append_cmake_bool(\@configure_command, "JPEG", $jpeg);
append_cmake_bool(\@configure_command, "EXAMPLES", $examples);
append_cmake_bool(\@configure_command, "FRACTAL_ZOOM", $fractal_zoom);
append_cmake_bool(\@configure_command, "PYTHON_MODULE", $python_module);
append_cmake_value(\@configure_command, "MXVK_PYTHON_INSTALL_DIR", $python_install_dir);
append_cmake_bool(\@configure_command, "MXVK_ENABLE_MOLTENVK", $moltenvk);
append_cmake_bool(\@configure_command, "MUTATRIS_INSTALL_USER_DESKTOP", $mutatris_desktop);
append_cmake_bool(\@configure_command, "ASTEROIDS3D_INSTALL_USER_DESKTOP", $asteroids3d_desktop);
append_cmake_bool(\@configure_command, "ASTEROIDS_NET_INSTALL_USER_DESKTOP", $asteroids_net_desktop);
append_cmake_value(\@configure_command, "GLSLC_EXECUTABLE", $glslc_executable);
push @configure_command, @ARGV;

my ($configure_output, $configure_status) = run_command("Configuring", @configure_command);
if ($configure_status != 0) {
    print STDERR "\nERROR: CMake configuration failed.\n";
    print_dependency_help($configure_output);
    exit $configure_status;
}

my (undef, $build_status) = run_command(
    "Building",
    "cmake", "--build", $build_dir, "--parallel", $jobs,
);
if ($build_status != 0) {
    print STDERR "\nERROR: The build failed. Review the first compiler or linker error above.\n";
    print STDERR "The configured build directory is: $build_dir\n";
    exit $build_status;
}

if (!$install) {
    print "\nBuild completed successfully; installation was skipped.\n";
    exit 0;
}

my $install_prefix = defined $prefix ? $prefix : read_install_prefix($build_dir);
$install_prefix = "/usr/local" unless defined $install_prefix && length $install_prefix;

my @install_command = ("cmake", "--install", $build_dir);
push @install_command, "--config", $configuration if defined $configuration;
push @install_command, "--component", $component if defined $component;
push @install_command, "--strip" if $strip_install;
my $needs_sudo = !prefix_is_writable($install_prefix) && $> != 0;

if (defined $use_sudo && $use_sudo) {
    fail("--sudo was requested, but sudo was not found.") unless command_path("sudo");
    unshift @install_command, "sudo";
} elsif ($needs_sudo) {
    if (defined $use_sudo && !$use_sudo) {
        fail("Install prefix '$install_prefix' is not writable. Choose --prefix DIR or omit --no-sudo.");
    }
    fail("Install prefix '$install_prefix' is not writable and sudo was not found. Choose --prefix DIR.")
        unless command_path("sudo");
    print "Install prefix '$install_prefix' requires administrator privileges; using sudo.\n";
    unshift @install_command, "sudo";
}

my (undef, $install_status) = run_command("Installing", @install_command);
if ($install_status != 0) {
    print STDERR "\nERROR: Installation failed for prefix '$install_prefix'.\n";
    print STDERR "Check the error above, or select a writable location with --prefix DIR.\n";
    exit $install_status;
}

print "\nMXVK was configured, built, and installed successfully to $install_prefix.\n";
exit 0;

sub default_jobs {
    my $count = 1;
    if ($^O eq "linux" && open(my $cpuinfo, "<", "/proc/cpuinfo")) {
        $count = grep { /^processor\s*:/ } <$cpuinfo>;
        close $cpuinfo;
    } elsif (command_path("sysctl")) {
        my ($output, $status) = capture_command("sysctl", "-n", "hw.ncpu");
        $count = $1 if $status == 0 && $output =~ /(\d+)/;
    }
    return $count > 0 ? $count : 1;
}

sub command_path {
    my ($command) = @_;
    return $command if File::Spec->file_name_is_absolute($command) && -x $command;
    for my $directory (File::Spec->path()) {
        my $candidate = File::Spec->catfile($directory, $command);
        return $candidate if -f $candidate && -x $candidate;
    }
    return;
}

sub capture_command {
    my (@command) = @_;
    my $pid = open(my $pipe, "-|", @command);
    return ("", 127) unless defined $pid;
    local $/;
    my $output = <$pipe> // "";
    close $pipe;
    return ($output, exit_status($?));
}

sub run_command {
    my ($label, @command) = @_;
    print "\n==> $label\n    " . join(" ", map { shell_quote($_) } @command) . "\n";

    my ($input, $output);
    my $error = gensym();
    my $pid;
    eval { $pid = open3($input, $output, $error, @command); 1 }
        or return ("Could not start command: $@", 127);
    close $input;

    my $selector = IO::Select->new($output, $error);
    my %is_error = (fileno($error) => 1);
    my $combined = "";
    while (my @ready = $selector->can_read()) {
        for my $handle (@ready) {
            my $read = sysread($handle, my $chunk, 8192);
            if (!defined $read || $read == 0) {
                $selector->remove($handle);
                close $handle;
                next;
            }
            $combined .= $chunk;
            my $destination = $is_error{fileno($handle)} ? *STDERR : *STDOUT;
            print {$destination} $chunk;
        }
    }
    waitpid($pid, 0);
    return ($combined, exit_status($?));
}

sub exit_status {
    my ($status) = @_;
    return 128 + ($status & 127) if $status & 127;
    return $status >> 8;
}

sub shell_quote {
    my ($value) = @_;
    return "''" if $value eq "";
    return $value if $value =~ m{^[A-Za-z0-9_./:=+,-]+$};
    $value =~ s/'/'"'"'/g;
    return "'$value'";
}

sub read_install_prefix {
    my ($directory) = @_;
    return read_cache_value($directory, "CMAKE_INSTALL_PREFIX");
}

sub append_cmake_bool {
    my ($command, $name, $value) = @_;
    push @$command, "-D$name=" . ($value ? "ON" : "OFF") if defined $value;
}

sub append_cmake_value {
    my ($command, $name, $value) = @_;
    push @$command, "-D$name=$value" if defined $value;
}

sub read_cache_value {
    my ($directory, $name) = @_;
    my $cache = File::Spec->catfile($directory, "CMakeCache.txt");
    return unless open(my $file, "<", $cache);
    while (my $line = <$file>) {
        if ($line =~ /^\Q$name\E:[^=]+=(.*)$/) {
            close $file;
            return $1;
        }
    }
    close $file;
    return;
}

sub prefix_is_writable {
    my ($path) = @_;
    $path = File::Spec->rel2abs($path);
    while (!-e $path) {
        my $parent = dirname($path);
        return 0 if $parent eq $path;
        $path = $parent;
    }
    return -d $path && -w $path;
}

sub print_dependency_help {
    my ($output) = @_;
    my @dependencies;
    my @patterns = (
        [qr/Could NOT find SDL3_ttf|package configuration file provided by ["']?SDL3_ttf/i, "SDL3_ttf development files"],
        [qr/Missing dependency:\s*SDL3\b/i, "SDL3 development files"],
        [qr/Missing dependency:\s*Vulkan|Could NOT find Vulkan/i, "Vulkan 1.4+ headers and loader"],
        [qr/glslc/i, "glslc (Vulkan shader compiler)"],
        [qr/(?:Could NOT find PNG|PNG_LIBRARY|png\.h)/i, "libpng development files"],
        [qr/(?:Could NOT find ZLIB|ZLIB_LIBRARY|zlib\.h)/i, "zlib development files"],
        [qr/(?:glmConfig|Could not find.*glm|glm\/glm\.hpp)/i, "glm development files"],
        [qr/Could NOT find OpenCV|Could not find.*OpenCVConfig/is, "OpenCV development files (requested by CV/CUDA options)"],
        [qr/WITH_MIXER=ON was requested.*SDL3_mixer/is, "SDL3_mixer development files (requested by WITH_MIXER=ON)"],
        [qr/(?:Could NOT find JPEG|jpe?g\.h)/i, "JPEG development files (requested by JPEG=ON)"],
        [qr/Could NOT find Boost|Could not find.*BoostConfig/is, "Boost development files (requested by FRACTAL_ZOOM=ON)"],
        [qr/WITH_MXWRITE=ON was requested.*FFmpeg/is, "FFmpeg development files (requested by WITH_MXWRITE=ON)"],
        [qr/WITH_CUDA=ON was requested.*CUDA/is, "CUDA toolkit (requested by WITH_CUDA=ON)"],
        [qr/a C\+\+ compiler|CMAKE_CXX_COMPILER.*not set/i, "a C++20 compiler"],
        [qr/(?:^|\n)cmake(?:\n|$)/i, "CMake 3.10 or newer"],
    );
    for my $entry (@patterns) {
        push @dependencies, $entry->[1] if $output =~ $entry->[0];
    }
    my %seen;
    @dependencies = grep { !$seen{$_}++ } @dependencies;

    if (@dependencies) {
        print STDERR "\nLikely missing requirements:\n";
        print STDERR "  - $_\n" for @dependencies;
        print STDERR "\nTypical core dependency packages:\n";
        if ($^O eq "darwin") {
            print STDERR "  brew install cmake sdl3 sdl3_ttf vulkan-loader vulkan-headers shaderc libpng glm\n";
        } elsif (linux_family() eq "arch") {
            print STDERR "  sudo pacman -S cmake gcc sdl3 sdl3_ttf vulkan-headers vulkan-icd-loader shaderc libpng zlib glm\n";
        } elsif (linux_family() eq "fedora") {
            print STDERR "  sudo dnf install cmake gcc-c++ SDL3-devel SDL3_ttf-devel vulkan-headers vulkan-loader-devel shaderc libpng-devel zlib-devel glm-devel\n";
        } else {
            print STDERR "  sudo apt install cmake g++ libsdl3-dev libsdl3-ttf-dev libvulkan-dev glslc libpng-dev zlib1g-dev libglm-dev\n";
        }
        print STDERR "Package names can vary by OS release. CMake's error above is authoritative.\n";
    }
}

sub linux_family {
    return "" unless $^O eq "linux";
    return "" unless open(my $file, "<", "/etc/os-release");
    local $/;
    my $release = <$file> // "";
    close $file;
    return "arch" if $release =~ /(?:^|\n)(?:ID|ID_LIKE)=[^\n]*arch/i;
    return "fedora" if $release =~ /(?:^|\n)(?:ID|ID_LIKE)=[^\n]*(?:fedora|rhel|centos)/i;
    return "debian" if $release =~ /(?:^|\n)(?:ID|ID_LIKE)=[^\n]*(?:debian|ubuntu)/i;
    return "";
}

sub fail {
    my ($message) = @_;
    print STDERR "ERROR: $message\n";
    exit 2;
}

sub usage {
    my ($status) = @_;
    print <<"USAGE";
Usage: ./install.pl [options] [CMake options]

Configure, build, and install MXVK. Unknown options are passed to CMake,
so standard CMake settings such as -G Ninja and -DCMAKE_TOOLCHAIN_FILE=... work
directly. Explicit install.pl feature options are translated to CMake cache values.

Options:
  -B, --build-dir DIR  Build directory (default: <source>/build)
      --prefix DIR     CMake installation prefix
  -j, --jobs N        Parallel build jobs (default: detected CPU count)
      --fresh          Start a fresh configure (requires CMake 3.24+)
      --no-install     Configure and build without installing
      --sudo           Always use sudo for installation
      --no-sudo        Never use sudo; fail if the prefix is not writable
      --config NAME    Install configuration for multi-config generators
      --component NAME Install only a named CMake install component
      --strip          Strip installed binaries when supported by CMake

MXVK feature options (use --no-NAME to disable boolean features):
      --debug                    Enable DEBUG_MODE compile flags
      --validation               Enable Vulkan validation layers
      --cv                       Enable OpenCV capture support
      --cuda AUTO|ON|OFF         CUDA acceleration/interop (default: AUTO)
      --eigen AUTO|ON|OFF        Eigen 3dmath backend (default: AUTO)
      --mxwrite AUTO|ON|OFF      MXWrite FFmpeg video writer (default: AUTO)
      --mixer AUTO|ON|OFF        SDL3_mixer audio support (default: AUTO)
      --jpeg                     Enable JPEG helpers
      --examples                 Build examples (default: ON)
      --fractal-zoom             Build the Boost fractal_zoom example
      --python-module            Build the nanobind mxvk_ext module
      --python-install-dir DIR   Python module install directory
      --glslc PATH               Path to the Vulkan glslc shader compiler
      --moltenvk                 Enable MoltenVK portability support (macOS only)

Example-install options (effective while examples are enabled):
      --mutatris-desktop         Install Mutatris desktop entry/icon (default: ON)
      --asteroids3d-desktop      Install asteroids3d desktop entry/icon (default: ON)
      --asteroids-net-desktop    Install asteroids-net desktop entry/icon (default: ON)
  -h, --help           Show this help

Examples:
  ./install.pl
  ./install.pl --prefix "\$HOME/.local" --no-examples --mixer OFF
  ./install.pl --fresh --validation --mixer ON
  ./install.pl --python-module --no-examples --python-install-dir lib/python3.12/site-packages
USAGE
    exit $status;
}
