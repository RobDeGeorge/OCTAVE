#!/usr/bin/env python3
"""
OCTAVE Setup Script
Cross-platform installer that handles dependencies and environment setup.

Usage:
    python setup.py            # Setup and run the app
    python setup.py --no-run   # Setup only, don't launch
"""

import subprocess
import sys
import os
import platform
import shutil
import time

# Ensure Unicode output works on Windows (cp1252 can't handle ✓/✗/→)
if sys.stdout.encoding and sys.stdout.encoding.lower().replace("-", "") != "utf8":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

def print_header(text):
    print("\n" + "=" * 50)
    print(f"  {text}")
    print("=" * 50)

def print_step(text):
    print(f"\n→ {text}")

def run_command(cmd, check=True, shell=False):
    """Run a command and return success status."""
    try:
        if isinstance(cmd, str) and not shell:
            cmd = cmd.split()
        subprocess.run(cmd, check=check, shell=shell)
        return True
    except subprocess.CalledProcessError:
        return False
    except FileNotFoundError:
        return False

def detect_platform():
    """Detect the current platform."""
    system = platform.system().lower()
    if system == "darwin":
        return "macos"
    elif system == "windows":
        return "windows"
    elif system == "linux":
        # Check if Raspberry Pi
        try:
            with open("/sys/firmware/devicetree/base/model") as f:
                if "raspberry" in f.read().lower():
                    return "raspberry"
        except OSError:
            pass
        return "linux"
    return "unknown"

def detect_linux_distro():
    """Detect the Linux distribution family."""
    # Check for os-release file (standard on modern Linux)
    try:
        with open("/etc/os-release") as f:
            os_release = f.read().lower()
            if "arch" in os_release or "manjaro" in os_release or "endeavouros" in os_release:
                return "arch"
            elif "debian" in os_release or "ubuntu" in os_release or "mint" in os_release or "pop" in os_release:
                return "debian"
            elif "fedora" in os_release or "rhel" in os_release or "centos" in os_release or "rocky" in os_release:
                return "fedora"
    except OSError:
        pass

    # Fallback: check for package managers
    if shutil.which("pacman"):
        return "arch"
    elif shutil.which("apt"):
        return "debian"
    elif shutil.which("dnf") or shutil.which("yum"):
        return "fedora"

    return "unknown"

def is_wsl():
    """Check if running in Windows Subsystem for Linux."""
    try:
        with open("/proc/version") as f:
            return "microsoft" in f.read().lower()
    except OSError:
        return False

def check_display():
    """Check if a display is available."""
    display = os.environ.get("DISPLAY")
    wayland = os.environ.get("WAYLAND_DISPLAY")
    return display is not None or wayland is not None

def install_linux_deps():
    """Install Linux system dependencies."""
    print_step("Installing system dependencies (requires sudo)...")

    distro = detect_linux_distro()
    print(f"  Detected distro family: {distro}")

    if distro == "arch":
        # Arch Linux / Manjaro / EndeavourOS packages
        packages = [
            # Python (venv is included in python on Arch)
            "python", "python-pip",

            # Audio
            "libpulse", "alsa-lib",

            # OpenGL/EGL
            "mesa", "libglvnd",

            # XCB platform plugin dependencies
            "libxcb", "xcb-util", "xcb-util-cursor", "xcb-util-image",
            "xcb-util-keysyms", "xcb-util-renderutil", "xcb-util-wm",

            # X11 and input
            "libx11", "libxkbcommon", "libxkbcommon-x11",
            "libxi", "libsm", "libice",

            # Fonts and rendering
            "fontconfig", "freetype2", "harfbuzz",

            # D-Bus
            "dbus",

            # Additional X11 libs
            "libxrender", "libxext", "libxrandr", "libxfixes", "libxcomposite",
            "libxdamage", "libxtst", "libxss",

            # Qt6 base (helps ensure PySide6 works)
            "qt6-base", "qt6-multimedia"
        ]

        run_command(["sudo", "pacman", "-S", "--needed", "--noconfirm"] + packages, check=False)

    elif distro == "fedora":
        # Fedora / RHEL / CentOS packages
        packages = [
            "python3", "python3-pip",
            "pulseaudio-libs", "alsa-lib",
            "mesa-libGL", "mesa-libEGL", "libglvnd-glx",
            "libxcb", "xcb-util", "xcb-util-cursor", "xcb-util-image",
            "xcb-util-keysyms", "xcb-util-renderutil", "xcb-util-wm",
            "libX11", "libX11-xcb", "libxkbcommon", "libxkbcommon-x11",
            "libXi", "libSM", "libICE",
            "fontconfig", "freetype", "harfbuzz",
            "dbus-libs",
            "libXrender", "libXext", "libXrandr", "libXfixes", "libXcomposite",
            "libXdamage", "libXtst", "libXScrnSaver",
            "qt6-qtbase", "qt6-qtmultimedia"
        ]

        run_command(["sudo", "dnf", "install", "-y"] + packages, check=False)

    else:
        # Debian / Ubuntu / Mint / Pop!_OS (default)
        packages = [
            # Python
            "python3", "python3-venv", "python3-pip",

            # Audio (PulseAudio for Qt multimedia)
            "libpulse0", "libasound2",

            # OpenGL/EGL (try multiple package names for compatibility)
            "libegl1", "libgl1", "libopengl0", "libglx-mesa0", "libgl1-mesa-dri",
            "libegl1-mesa", "libgles2",

            # XCB platform plugin dependencies (Qt6 xcb plugin)
            "libxcb1", "libxcb-cursor0", "libxcb-icccm4", "libxcb-image0",
            "libxcb-keysyms1", "libxcb-randr0", "libxcb-render0", "libxcb-render-util0",
            "libxcb-shape0", "libxcb-shm0", "libxcb-sync1", "libxcb-xfixes0",
            "libxcb-xinerama0", "libxcb-xkb1", "libxcb-util1",

            # X11 and input
            "libx11-6", "libx11-xcb1", "libxkbcommon0", "libxkbcommon-x11-0",
            "libxi6", "libsm6", "libice6",

            # Fonts and rendering
            "libfontconfig1", "libfreetype6", "libharfbuzz0b",

            # D-Bus (for system integration)
            "libdbus-1-3",

            # Additional libs that Qt might need
            "libxrender1", "libxext6", "libxrandr2", "libxfixes3", "libxcomposite1",
            "libxdamage1", "libxtst6", "libxss1"
        ]

        # Update package list
        run_command(["sudo", "apt", "update"])

        # Install all packages at once (faster), ignore errors for missing packages
        run_command(["sudo", "apt", "install", "-y"] + packages, check=False)

    print("✓ System dependencies installed")

def check_python_version():
    """Check if Python version is adequate."""
    version = sys.version_info
    if version.major < 3 or (version.major == 3 and version.minor < 10):
        print(f"✗ Python 3.10+ required, found {version.major}.{version.minor}")
        sys.exit(1)
    print(f"✓ Python {version.major}.{version.minor}.{version.micro}")

def setup_venv():
    """Create virtual environment if it doesn't exist."""
    print_step("Setting up virtual environment...")

    venv_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "venv")

    if os.path.exists(venv_path):
        # Check if venv is compatible with current platform and Python version
        venv_valid = False
        pyvenv_cfg = os.path.join(venv_path, "pyvenv.cfg")

        if os.path.exists(pyvenv_cfg):
            try:
                with open(pyvenv_cfg, "r") as f:
                    cfg_content = f.read()

                # Check if venv was created on a different OS
                is_windows_venv = "C:\\" in cfg_content or "c:\\" in cfg_content
                current_is_windows = platform.system() == "Windows"

                if is_windows_venv != current_is_windows:
                    print(f"  Existing venv is from {'Windows' if is_windows_venv else 'Linux/macOS'}, recreating...")
                else:
                    # Check that pip actually works (not just that the shim exists)
                    pip_path = get_pip_path(venv_path)
                    if os.path.exists(pip_path) and run_command([pip_path, "--version"], check=True):
                        venv_valid = True
                        print("✓ Virtual environment already exists")
                    else:
                        print("  Existing venv has broken pip, recreating...")
            except Exception:
                print("  Could not read venv config, recreating...")
        else:
            print("  Existing venv is incomplete, recreating...")

        if not venv_valid:
            # Remove incompatible venv
            def _remove_readonly(func, path, exc_info):
                """Handle permission errors on Windows (read-only or locked files)."""
                import stat
                try:
                    os.chmod(path, stat.S_IWRITE)
                    func(path)
                except Exception:
                    pass  # Skip files that are truly locked by another process

            try:
                shutil.rmtree(venv_path, onexc=_remove_readonly)
            except Exception:
                # If rmtree still fails, try renaming and moving on
                fallback = venv_path + f"_old_{int(time.time())}"
                try:
                    os.rename(venv_path, fallback)
                    print(f"  Could not fully remove old venv (files locked), renamed to {os.path.basename(fallback)}")
                except Exception:
                    print("✗ Could not remove old venv — close any programs using it and try again.")
                    sys.exit(1)

    if not os.path.exists(venv_path):
        try:
            import venv
            venv.create(venv_path, with_pip=True)
            print("✓ Virtual environment created")
        except Exception as e:
            print(f"✗ Failed to create venv: {e}")
            distro = detect_linux_distro() if platform.system().lower() == "linux" else None
            if distro == "arch":
                print("\nTry installing python:")
                print("  sudo pacman -S python")
            elif distro == "debian":
                print("\nTry installing python3-venv:")
                print("  sudo apt install python3-venv")
            sys.exit(1)

    return venv_path

def get_pip_path(venv_path):
    """Get the pip executable path for the venv."""
    if platform.system() == "Windows":
        return os.path.join(venv_path, "Scripts", "pip.exe")
    return os.path.join(venv_path, "bin", "pip")

def get_python_path(venv_path):
    """Get the python executable path for the venv."""
    if platform.system() == "Windows":
        return os.path.join(venv_path, "Scripts", "python.exe")
    return os.path.join(venv_path, "bin", "python")

def install_python_deps(venv_path):
    """Install Python dependencies."""
    print_step("Installing Python dependencies...")

    pip_path = get_pip_path(venv_path)
    requirements = os.path.join(os.path.dirname(os.path.abspath(__file__)), "requirements.txt")

    # Upgrade pip first
    run_command([pip_path, "install", "--upgrade", "pip"], check=False)

    # Install requirements
    if run_command([pip_path, "install", "-r", requirements]):
        print("✓ Python dependencies installed")
    else:
        print("✗ Failed to install some dependencies")
        sys.exit(1)

def run_app(venv_path):
    """Run the OCTAVE application."""
    print_header("Starting OCTAVE")

    python_path = get_python_path(venv_path)
    main_py = os.path.join(os.path.dirname(os.path.abspath(__file__)), "main.py")

    os.execv(python_path, [python_path, main_py])

def main():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    print_header("OCTAVE Setup")

    # Detect platform
    plat = detect_platform()
    wsl = is_wsl()
    print(f"Platform: {plat}{' (WSL)' if wsl else ''}")

    # Check Python version
    check_python_version()

    # Install system dependencies on Linux
    if plat in ("linux", "raspberry"):
        install_linux_deps()
    elif plat == "macos":
        print_step("macOS detected - system dependencies should be available")
    elif plat == "windows":
        print_step("Windows detected - no system dependencies needed")

    # Setup virtual environment
    venv_path = setup_venv()

    # Install Python dependencies
    install_python_deps(venv_path)

    print_header("Setup Complete!")

    # Show run instructions
    if plat == "windows":
        activate_cmd = r"venv\Scripts\activate"
    else:
        activate_cmd = "source venv/bin/activate"

    if "--no-run" in sys.argv:
        print(f"""
To run OCTAVE:
    {activate_cmd}
    python main.py
""")

    # WSL-specific guidance
    if wsl and not check_display():
        print("=" * 50)
        print("  WSL Note: No display detected")
        print("=" * 50)
        print("""
OCTAVE requires a display to run. Options:

1. WSLg (Windows 11) - Should work automatically
   Try: wsl --update

2. X Server (Windows 10) - Install VcXsrv or X410
   Then run: export DISPLAY=:0

3. Run on Windows directly instead of WSL
""")

    # Run the app unless --no-run flag is passed
    if "--no-run" not in sys.argv:
        if wsl and not check_display():
            print("Skipping app launch - no display available.")
            print("Set up a display first, then run: python main.py")
        else:
            run_app(venv_path)

if __name__ == "__main__":
    main()
