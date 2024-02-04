set -eu

echo -n "Exporting custom recipes..."
echo -n "folly."
conan export 3rd_party/folly --name folly --version nu2.2023.12.18.00 >/dev/null
echo -n "folly."
conan export 3rd_party/userspace-rcu --name userspace-rcu --version nu2.0.14.0 >/dev/null
echo "done."
