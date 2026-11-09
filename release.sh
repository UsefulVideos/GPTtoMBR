#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
tag="${1:-}"
target_repo="${2:-}"
target_remote="${3:-}"

if [[ ! "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
   { [[ -n "$target_repo" ]] && [[ ! "$target_repo" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]]; }; then
    echo "Usage: $0 vMAJOR.MINOR.PATCH [OWNER/REPOSITORY [GIT_REMOTE]]" >&2
    exit 2
fi

for tool in git cmake gh sha256sum tr; do
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

gh auth status --hostname github.com
if [[ -z "$target_repo" ]]; then
    github_user="$(gh api user --jq .login)"
    if [[ ! "$github_user" =~ ^[A-Za-z0-9-]+$ ]]; then
        echo "Error: could not determine the authenticated GitHub user." >&2
        exit 1
    fi

    if [[ "$(printf '%s' "$github_user" | tr '[:upper:]' '[:lower:]')" == "usefulvideos" ]]; then
        target_repo="UsefulVideos/GPTtoMBR"
        target_remote="origin"
    else
        target_repo="$github_user/GPTtoMBR"
        target_remote="user-fork"
        if gh api "repos/$target_repo" >/dev/null 2>&1; then
            fork_parent="$(gh api "repos/$target_repo" --jq '.parent.full_name // ""')"
            if [[ "$fork_parent" != "UsefulVideos/GPTtoMBR" ]]; then
                echo "Error: $target_repo exists but is not a fork of UsefulVideos/GPTtoMBR." >&2
                exit 1
            fi
        else
            gh repo fork UsefulVideos/GPTtoMBR --clone=false --remote --remote-name "$target_remote"
        fi

        if ! git -C "$repo_root" remote get-url "$target_remote" >/dev/null 2>&1; then
            git -C "$repo_root" remote add "$target_remote" "https://github.com/$target_repo.git"
        fi
        fork_remote_url="$(git -C "$repo_root" remote get-url "$target_remote")"
        case "$fork_remote_url" in
            "https://github.com/$target_repo"|"https://github.com/$target_repo.git"|\
            "git@github.com:$target_repo.git"|"ssh://git@github.com/$target_repo.git")
                ;;
            *)
                echo "Error: remote '$target_remote' does not point to $target_repo." >&2
                exit 1
                ;;
        esac
    fi
fi

target_remote="${target_remote:-origin}"
if [[ ! "$target_repo" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]]; then
    echo "Error: target repository must be in OWNER/REPOSITORY form." >&2
    exit 2
fi

branch="$(git -C "$repo_root" branch --show-current)"
if [[ -z "$branch" ]]; then
    echo "Error: release must run from a named branch." >&2
    exit 1
fi

local_commit="$(git -C "$repo_root" rev-parse HEAD)"
remote_commit="$(git -C "$repo_root" ls-remote "$target_remote" "refs/heads/$branch" | cut -f1)"
if [[ -z "$remote_commit" || "$local_commit" != "$remote_commit" ]]; then
    echo "Error: push the current branch to '$target_remote' before creating a release." >&2
    exit 1
fi

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
    --repo "$target_repo" \
    --title "GptToMbr $tag" \
    --generate-notes \
    --target "$branch"
