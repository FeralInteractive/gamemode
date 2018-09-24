#!/bin/bash
# Simple script to check for clang-format compliance

wget -Nq https://llvm.org/svn/llvm-project/cfe/trunk/tools/clang-format/git-clang-format

if chmod +x git-clang-format; then
  CLANG_FORMAT_OUTPUT=$(./git-clang-format HEAD^ HEAD --diff)
  if [[ ! ${CLANG_FORMAT_OUTPUT} == "no modified files to format" ]] && [[ ! -z ${CLANG_FORMAT_OUTPUT} ]]; then
    echo "Failed clang format check:"
    echo "${CLANG_FORMAT_OUTPUT}"
    exit 1
  else
    echo "Passed clang format check"
  fi
else
  echo "git-clang-format not downloaded"
  exit 1
fi
