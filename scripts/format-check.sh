#!/bin/bash
# Simple script to check for clang-format compliance
wget https://llvm.org/svn/llvm-project/cfe/trunk/tools/clang-format/git-clang-format
chmod +x git-clang-format

CLANG_FORMAT_OUTPUT=$(./git-clang-format HEAD^ HEAD --diff)
if [[ ! $CLANG_FORMAT_OUTPUT == "no modified files to format" ]]; then
  echo "Failed clang format check:"
  echo "${CLANG_FORMAT_OUTPUT}"
  exit 1
else
  echo "Passed clang format check"
fi

