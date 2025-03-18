# This script needs to be sourced, not executed

while IFS= read -r dir; do
    export PATH="$dir:$PATH"
done < <(find "$(realpath toolchains)" -maxdepth 2 -type d -name "bin")
