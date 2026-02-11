import sys
import pkg_resources
import subprocess

def check_packages(packages):
    missing_packages = []
    for package in packages:
        try:
            pkg_resources.require(package)
        except (pkg_resources.DistributionNotFound, pkg_resources.VersionConflict):
            missing_packages.append(package)
    return missing_packages

def install_packages(packages):
    for package in packages:
        try:
            print(f"Installing {package}...")
            subprocess.check_call([sys.executable, "-m", "pip", "install", package])
        except subprocess.CalledProcessError:
            print(f"Failed to install {package}.")
            return False
    return True

# Nanopb in this tree relies on google.protobuf.reflection.MakeClass,
# which is not available in newer protobuf releases.
required_packages = ["protobuf>=3.20,<5"]
missing_packages = check_packages(required_packages)

if missing_packages:
    print("Missing required Python packages:", ", ".join(missing_packages))
    if install_packages(missing_packages):
        missing_packages = check_packages(required_packages)
        if missing_packages:
            print("Still missing required Python packages after installation attempt:", ", ".join(missing_packages))
            sys.exit(1)
    else:
        sys.exit(1)

print("All required Python packages are installed.")

# Check for the marker file path argument
if len(sys.argv) < 2:
    print("Error: No marker file path provided.")
    sys.exit(1)

marker_file_path = sys.argv[1]
with open(marker_file_path, "w") as marker_file:
    marker_file.write("Python packages check completed successfully.")

sys.exit(0)
