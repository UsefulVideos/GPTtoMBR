#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
tag="${1:-}"

if [[ ! "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Usage: $0 vMAJOR.MINOR.PATCH" >&2
    exit 2
fi

for tool in git cmake gh sha256sum; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Error: required command '$tool' was not found." >&2
        exit 1
    fi
done

mingw_cxx="${MINGW_CXX:-x86_64-w64-mingw32-g++}"
if ! command -v "$mingw_cxx" >/dev/null 2>&1; then
    echo "Error: MinGW-w64 compiler '$mingw_cxx' was not found (set MINGW_CXX to override)." >&2
    exit 1
fi

if [[ -n "$(git -C "$repo_root" status --porcelain)" ]]; then
    echo "Error: commit or stash all changes before creating a release." >&2
    exit 1
fi

branch="$(git -C "$repo_root" branch --show-current)"
if [[ -z "$branch" ]]; then
    echo "Error: release must run from a named branch." >&2
    exit 1
fi

local_commit="$(git -C "$repo_root" rev-parse HEAD)"
remote_commit="$(git -C "$repo_root" ls-remote origin "refs/heads/$branch" | cut -f1)"
if [[ -z "$remote_commit" || "$local_commit" != "$remote_commit" ]]; then
    echo "Error: push the current branch to origin before creating a release." >&2
    exit 1
fi

gh auth status --hostname github.com

build_dir="$repo_root/build/release-windows"
artifact_dir="$build_dir/artifacts"
cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
    -DCMAKE_CXX_COMPILER="$mingw_cxx" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$artifact_dir" \
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE="$artifact_dir"
cmake --build "$build_dir" --config Release --target GptToMbr

executable="$artifact_dir/GptToMbr.exe"
if [[ ! -f "$executable" ]]; then
    echo "Error: expected release executable was not produced at $executable." >&2
    exit 1
fi

asset="$artifact_dir/GptToMbr-${tag}-windows-x86_64.exe"
cp "$executable" "$asset"
checksum="$asset.sha256"
(cd "$artifact_dir" && sha256sum "$(basename "$asset")" > "$(basename "$checksum")")

gh release create "$tag" "$asset" "$checksum" \
    --repo UsefulVideos/GPTtoMBR \
    --title "GptToMbr $tag" \
    --generate-notes \
    --target "$branch"
