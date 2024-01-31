set -eu

echo -n "Exporting custom recipes..."
echo -n "breakpad."
conan export 3rd_party/breakpad --name breakpad --version cci.20230127 >/dev/null
echo -n "folly."
conan export 3rd_party/folly --name folly --version nu2.2023.12.18.00 >/dev/null
echo -n "prerelease_dummy."
conan export 3rd_party/prerelease_dummy --name prerelease_dummy --version 1.0.1 >/dev/null
echo "done."
