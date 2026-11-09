#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$repo_root"

for tool in git make gh grep sort tail tr; do
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
github_user="$(gh api user --jq .login)"
if [[ ! "$github_user" =~ ^[A-Za-z0-9-]+$ ]]; then
    echo "Error: could not determine the authenticated GitHub user." >&2
    exit 1
fi

parent_repo="UsefulVideos/GPTtoMBR"
if [[ "$(printf '%s' "$github_user" | tr '[:upper:]' '[:lower:]')" == "usefulvideos" ]]; then
    target_repo="$parent_repo"
    push_remote="origin"
else
    target_repo="$github_user/GPTtoMBR"
    push_remote="user-fork"

    if gh api "repos/$target_repo" >/dev/null 2>&1; then
        fork_parent="$(gh api "repos/$target_repo" --jq '.parent.full_name // ""')"
        if [[ "$fork_parent" != "$parent_repo" ]]; then
            echo "Error: $target_repo already exists but is not a fork of $parent_repo." >&2
            exit 1
        fi
    else
        echo "Creating fork $target_repo from $parent_repo..."
        gh repo fork "$parent_repo" --clone=false --remote --remote-name "$push_remote"
    fi

    fork_url="https://github.com/$target_repo.git"
    if git remote get-url "$push_remote" >/dev/null 2>&1; then
        existing_fork_url="$(git remote get-url "$push_remote")"
        case "$existing_fork_url" in
            "https://github.com/$target_repo"|"https://github.com/$target_repo.git"|\
            "git@github.com:$target_repo.git"|"ssh://git@github.com/$target_repo.git")
                ;;
            *)
                echo "Error: remote '$push_remote' points to '$existing_fork_url', not $target_repo." >&2
                exit 1
                ;;
        esac
    else
        git remote add "$push_remote" "$fork_url"
    fi
fi

origin_url="$(git remote get-url origin)"
case "$origin_url" in
    https://github.com/UsefulVideos/GPTtoMBR|https://github.com/UsefulVideos/GPTtoMBR.git|\
    git@github.com:UsefulVideos/GPTtoMBR.git|ssh://git@github.com/UsefulVideos/GPTtoMBR.git|\
    "https://github.com/$target_repo"|"https://github.com/$target_repo.git"|\
    "git@github.com:$target_repo.git"|"ssh://git@github.com/$target_repo.git")
        ;;
    *)
        echo "Error: origin must point to $parent_repo or the authenticated user's fork (found '$origin_url')." >&2
        exit 1
        ;;
esac

git fetch --tags origin
if [[ "$push_remote" != "origin" ]]; then
    git fetch --tags "$push_remote"
fi

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

if [[ -n "$(git ls-remote --tags "$push_remote" "refs/tags/$release_tag")" ]]; then
    echo "Error: release tag '$release_tag' already exists on $target_repo." >&2
    exit 1
fi

make clean all

git add -u
git add -- build-and-release.sh
if ! git diff --cached --quiet; then
    git -c user.name="Copilot" \
        -c user.email="223556219+Copilot@users.noreply.github.com" \
        commit -m "Prepare release $release_tag" \
        -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
fi

git push "$push_remote" "$branch"
bash "$repo_root/release.sh" "$release_tag" "$target_repo" "$push_remote"
