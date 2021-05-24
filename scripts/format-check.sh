#!/bin/bash
# Simple script to check for clang-format compliance

# Ensure we are at the project root
cd "$(dirname $0)"/..

if [[ "$1" == "--pre-commit" ]]; then
  # used via .git/hooks/pre-commit:
  # exec "$(dirname $0)"/../../scripts/format-check.sh --pre-commit
  git-clang-format
  exit
fi

if [[ "$CI" == "true" ]]; then
  # used in ci, assumes clean repo
  clang-format -i $(find . -name '*.[ch]' -not -path "*subprojects/*")
  GIT_DIFF_OUTPUT=$(git diff)
  if [[ ! -z ${GIT_DIFF_OUTPUT} ]]; then
    echo "Failed clang format check:"
    echo "${GIT_DIFF_OUTPUT}"
    exit 1
  else
    echo "Passed clang format check"
    exit 0
  fi
fi

CLANG_FORMAT_OUTPUT=$(git-clang-format HEAD^ HEAD --diff)
if [[ ! ${CLANG_FORMAT_OUTPUT} == "no modified files to format" ]] && [[ ! -z ${CLANG_FORMAT_OUTPUT} ]]; then
  echo "Failed clang format check:"
  echo "${CLANG_FORMAT_OUTPUT}"
  exit 1
else
  echo "Passed clang format check"
  exit 0
fi
