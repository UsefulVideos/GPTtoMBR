#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$repo_root"

for tool in git make gh grep sort tail; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Error: required command '$tool' was not found." >&2
        exit 1
    fi
done

branch="$(git branch --show-current)"
if [[ -z "$branch" ]]; then
    echo "Error: build and release must run from a named branch." >&2
    exit 1
fi

origin_url="$(git remote get-url origin)"
case "$origin_url" in
    https://github.com/UsefulVideos/GPTtoMBR|https://github.com/UsefulVideos/GPTtoMBR.git|git@github.com:UsefulVideos/GPTtoMBR.git|ssh://git@github.com/UsefulVideos/GPTtoMBR.git)
        ;;
    *)
        echo "Error: origin must point to UsefulVideos/GPTtoMBR (found '$origin_url')." >&2
        exit 1
        ;;
esac

if ! git diff --cached --quiet; then
    echo "Error: uncommitted staged changes found; unstage or commit them before building and releasing." >&2
    exit 1
fi

while IFS= read -r path; do
    if [[ "$path" != "build-and-release.sh" ]]; then
        echo "Error: untracked file '$path' found; add or ignore it before building and releasing." >&2
        exit 1
    fi
done < <(git ls-files --others --exclude-standard)

gh auth status --hostname github.com
git fetch --tags origin

latest_tag="$(git tag --list | grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' | sort -V | tail -n 1 || true)"
if [[ -z "$latest_tag" ]]; then
    release_tag="v1.0.0"
else
    [[ "$latest_tag" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]
    major="${BASH_REMATCH[1]}"
    minor="${BASH_REMATCH[2]}"
    patch="${BASH_REMATCH[3]}"
    release_tag="v${major}.${minor}.$((10#$patch + 1))"
fi

if [[ -n "$(git ls-remote --tags origin "refs/tags/$release_tag")" ]]; then
    echo "Error: release tag '$release_tag' already exists on origin." >&2
    exit 1
fi

make clean all

git add -u
git add -- build-and-release.sh
if ! git diff --cached --quiet; then
    git commit -m "Prepare release $release_tag"
fi

git push origin "$branch"
bash "$repo_root/release.sh" "$release_tag"
