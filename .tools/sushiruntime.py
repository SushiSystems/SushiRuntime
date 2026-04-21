import os
import sys
import subprocess
import platform
import argparse
import shutil
from pathlib import Path

class Colors:
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    GREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

class SRPythonManager:
    def __init__(self):
        # We are now in .tools/ folder, so root is parent
        self.tools_dir = Path(__file__).parent.absolute()
        self.root_dir = self.tools_dir.parent.absolute()
        self.os_name = platform.system().lower() # 'windows', 'linux'
        self.build_dir = self.root_dir / "build"
        
    def _print_info(self, msg):
        print(f"{Colors.BLUE}{Colors.BOLD}[INFO]{Colors.ENDC} {msg}")

    def _print_success(self, msg):
        print(f"{Colors.GREEN}{Colors.BOLD}[SUCCESS]{Colors.ENDC} {msg}")

    def _print_error(self, msg):
        print(f"{Colors.FAIL}{Colors.BOLD}[ERROR]{Colors.ENDC} {msg}")

    def _run_command(self, cmd, cwd=None):
        if cwd is None: cwd = self.root_dir
        self._print_info(f"Executing: {Colors.CYAN}{cmd}{Colors.ENDC}")
        try:
            # Use shell=True for script execution compatibility
            subprocess.run(cmd, shell=True, check=True, cwd=str(cwd))
            return True
        except subprocess.CalledProcessError as e:
            self._print_error(f"Command failed with exit code {e.returncode}")
            return False

    def docker_build(self):
        print(f"\n{Colors.HEADER}--- Docker Build ---{Colors.ENDC}")
        return self._run_command("docker build -t sushiruntime .")

    def docker_run(self):
        print(f"\n{Colors.HEADER}--- Docker Run ---{Colors.ENDC}")
        root_path = str(self.root_dir)
        v_mount = f"{root_path}:/workspace/sushiruntime"
        return self._run_command(f"docker run -it --rm -v \"{v_mount}\" sushiruntime /bin/bash")

    def project_clean(self):
        if self.build_dir.exists():
            self._print_info(f"Removing {self.build_dir}...")
            try:
                shutil.rmtree(self.build_dir)
                self._print_success("Build directory cleaned.")
            except Exception as e:
                self._print_error(f"Failed to remove build directory: {e}")
        else:
            self._print_info("Build directory does not exist, nothing to clean.")

    def project_build(self, clean=False):
        print(f"\n{Colors.HEADER}--- Project Build ---{Colors.ENDC}")
        if clean:
            self.project_clean()
            
        script_ext = "bat" if self.os_name == "windows" else "sh"
        script_path = self.tools_dir / "project" / f"build.{script_ext}"
        
        if not script_path.exists():
            self._print_error(f"Could not find {script_path}")
            return False
            
        cmd = f"\"{script_path}\"" if self.os_name == "windows" else f"bash \"{script_path}\""
        return self._run_command(cmd)

    def project_run(self, target=None, sort=False):
        print(f"\n{Colors.HEADER}--- Project Run ---{Colors.ENDC}")
        script_ext = "bat" if self.os_name == "windows" else "sh"
        script_path = self.tools_dir / "project" / f"run.{script_ext}"
        
        cmd = f"\"{script_path}\"" if self.os_name == "windows" else f"bash \"{script_path}\""
            
        if sort:
            cmd += " --sort"
        elif target:
            cmd += f" {target}"
            
        return self._run_command(cmd)

    def project_doxygen(self):
        print(f"\n{Colors.HEADER}--- Doxygen Generation ---{Colors.ENDC}")
        script_ext = "bat" if self.os_name == "windows" else "sh"
        script_path = self.tools_dir / "project" / f"doxygen.{script_ext}"
        
        cmd = f"\"{script_path}\"" if self.os_name == "windows" else f"bash \"{script_path}\""
            
        return self._run_command(cmd)

class Parser(argparse.ArgumentParser):
    def error(self, message):
        print(f"\n{Colors.FAIL}{Colors.BOLD}SushiRuntime CLI Error!{Colors.ENDC}")
        print(f"{Colors.WARNING}Message:{Colors.ENDC} {message}")
        print(f"\n{Colors.CYAN}Try using '--help' for available options.{Colors.ENDC}\n")
        sys.exit(2)

def main():
    manager = SRPythonManager()
    parser = Parser(
        description=f"{Colors.BOLD}{Colors.HEADER}SushiRuntime Script Manager{Colors.ENDC}",
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    subparsers = parser.add_subparsers(dest="category", title="Available Commands")

    # Docker commands
    docker_parser = subparsers.add_parser("docker", help="Docker environment management")
    docker_parser.add_argument("action", choices=["build", "run"], help="Choose: build | run")

    # Project commands
    project_parser = subparsers.add_parser("project", help="C++ code management and execution")
    project_parser.add_argument("action", choices=["build", "run", "doxygen", "clean"], help="Choose: build | run | doxygen | clean")
    project_parser.add_argument("target", nargs='?', help="Specific executable name to run (optional)")
    project_parser.add_argument("--clean", action="store_true", help="Delete build folder before build")
    project_parser.add_argument("--sort", action="store_true", help="Show interactive menu to select executable")

    if len(sys.argv) == 1:
        parser.print_help()
        sys.exit(0)

    args = parser.parse_args()

    if args.category == "docker":
        if args.action == "build": manager.docker_build()
        elif args.action == "run": manager.docker_run()
    elif args.category == "project":
        if args.action == "build": manager.project_build(clean=args.clean)
        elif args.action == "run": manager.project_run(target=args.target, sort=args.sort)
        elif args.action == "doxygen": manager.project_doxygen()
        elif args.action == "clean": manager.project_clean()
    else:
        parser.print_help()

if __name__ == "__main__":
    # Support ANSI colors on Windows CMD
    if platform.system().lower() == 'windows':
        os.system('color')
    main()
