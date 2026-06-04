#!/bin/bash

sed -E \
  -e 's/return ffi_safe_call<[^>]+>\([^,]*, *\[[^]]*\] *\{(.*)\}\);/{\1}/g' \
  -e 's/return ffi_safe_call<[^>]+>\([^,]*, *\[[^]]*\] *\{/{/g' \
  -e 's/ffi_safe_exec\(\[[^]]*\] *\{(.*)\}\);/{\1}/g' \
  -e 's/ffi_safe_exec\(\[[^]]*\] *\{/{/g' \
  -e 's/^([[:space:]]*)\}\);[[:space:]]*$/\1}/g' \
  "$1"
