#!/bin/sh
# Eric Radman, 2019

set -e

cd "$(dirname $0)/.."
tmp=$(mktemp)
trap 'rm $tmp' EXIT

cat schema/01-roles.sql > $tmp
ssh ${1:-localhost} pg_dump -s -U pgprobe >> $tmp
cat schema/*.sql | ddl_compare $tmp -

