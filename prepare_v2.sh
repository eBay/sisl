set -eu

echo -n "Exporting custom recipes..."
echo -n "userspace rcu."
conan export 3rd_party/userspace-rcu --name userspace-rcu --version nu2.0.14.0 >/dev/null
echo -n " pistache."
conan export 3rd_party/pistache --name pistache --version nbi.0.0.5.1 >/dev/null
echo " done."
