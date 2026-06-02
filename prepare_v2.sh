set -eu

echo -n "Exporting custom recipes..."
echo -n "userspace rcu."
conan export 3rd_party/userspace-rcu --name userspace-rcu --version nu2.0.14.0 >/dev/null
echo -n "stdexec."
conan export 3rd_party/stdexec --name stdexec --version 25.09 >/dev/null
echo " done."
