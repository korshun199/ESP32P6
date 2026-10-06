#!/usr/bin/env bash

# Запускает Git для постоянного локального хранилища при недоступном .git.
set -Eeuo pipefail

# Корень рабочего дерева и каталог служебных файлов Git.
PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
GIT_STORAGE="$PROJECT_DIR/.git-local"

if [[ ! -d "$GIT_STORAGE" ]]; then
    printf 'Локальное хранилище Git не найдено: %s\n' "$GIT_STORAGE" >&2
    exit 1
fi

exec git --git-dir="$GIT_STORAGE" --work-tree="$PROJECT_DIR" "$@"
