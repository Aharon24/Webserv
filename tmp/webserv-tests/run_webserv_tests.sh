#!/usr/bin/env bash
# 42 Webserv test launcher. Only runs tests; never edits project sources.

# Directory containing this launcher and its test files
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

set -u

TEST_DIR="$SCRIPT_DIR"
TEST_SOURCE="$TEST_DIR/upload_test_runner.cpp"
TEST_BINARY="$TEST_DIR/upload_tests"
PROJECT_DIR="${1:-$PWD}"

if [[ -t 1 ]]; then
    RESET=$'\033[0m'
    BOLD=$'\033[1m'
    CYAN=$'\033[36m'
    GREEN=$'\033[32m'
    RED=$'\033[31m'
    YELLOW=$'\033[33m'
else
    RESET=''; BOLD=''; CYAN=''; GREEN=''; RED=''; YELLOW=''
fi

upload_tests() {
    if [[ ! -f "$PROJECT_DIR/src/Upload.cpp" || ! -f "$PROJECT_DIR/hpp/Upload.hpp" ]]; then
        printf '%s[ERROR]%s Не найдены src/Upload.cpp или hpp/Upload.hpp в %s\n' "$RED" "$RESET" "$PROJECT_DIR"
        return 1
    fi
    if [[ ! -f "$TEST_SOURCE" ]]; then
        printf '%s[ERROR]%s Не найден тестер: %s\n' "$RED" "$RESET" "$TEST_SOURCE"
        return 1
    fi

    printf '\n%s[BUILD]%s Компилирую Upload tests (C++98)...\n' "$CYAN" "$RESET"
    if ! c++ -Wall -Wextra -Werror -std=c++98 \
        -I"$PROJECT_DIR/hpp" \
        "$TEST_SOURCE" "$PROJECT_DIR/src/Upload.cpp" \
        -o "$TEST_BINARY"; then
        printf '%s[FAIL]%s Ошибка компиляции.\n' "$RED" "$RESET"
        return 1
    fi
    printf '%s[OK]%s Сборка успешна. Запускаю тестер.\n' "$GREEN" "$RESET"
    "$TEST_BINARY"
}

if [[ ! -d "$PROJECT_DIR" ]]; then
    printf 'Папка проекта не найдена: %s\n' "$PROJECT_DIR" >&2
    exit 1
fi
mkdir -p "$TEST_DIR" || exit 1

while true; do
    printf '\n%s%s' "$CYAN" "$BOLD"
    printf '╔══════════════════════════════════╗\n'
    printf '║      42 WEBSERV TEST RUNNER      ║\n'
    printf '╚══════════════════════════════════╝\n'
    printf '%s' "$RESET"
    printf '  Проект: %s\n\n' "$PROJECT_DIR"
    printf '  %s1)%s Upload tests\n' "$GREEN" "$RESET"
    printf '  %s0)%s Exit\n\n' "$YELLOW" "$RESET"
    read -r -p '  Выбор > ' choice || { printf '\n'; exit 0; }
    case "$choice" in
        1) upload_tests ;;
        0) printf 'До встречи!\n'; exit 0 ;;
        *) printf '%s[ERROR]%s Неизвестный пункт меню.\n' "$RED" "$RESET" ;;
    esac
done
