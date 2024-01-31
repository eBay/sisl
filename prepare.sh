set -eu

echo -n "Exporting custom recipes..."
echo -n "breakpad."
conan export 3rd_party/breakpad breakpad/cci.20230127@ >/dev/null
echo -n "folly."
conan export 3rd_party/folly folly/nu2.2023.12.18.00@ >/dev/null
echo -n "prerelease_dummy."
conan export 3rd_party/prerelease_dummy >/dev/null
echo "done."
