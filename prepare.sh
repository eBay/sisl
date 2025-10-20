set -eu

echo -n "Exporting custom recipes..."
echo -n "userpace rcu."
conan export 3rd_party/userspace-rcu userspace-rcu/nu2.0.14.0@ >/dev/null
echo "done."
