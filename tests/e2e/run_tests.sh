#!/bin/sh
# run_tests.sh — zimg e2e suite (both engines must agree).
#
# Why not `zz test`: the runner type-checks plugin imports but never
# dlopens build/*.so, so native calls fail at runtime with
# "undefined variable `zimg_*`" (zz_lang gap, noted in README).
# Until that lands, each check file is a `zz run` program asserting
# internally (nonzero exit / missing ALL_PASS = failure), plus an
# AOT leg (`zz build` + run binary) per file.
#
# Usage: ./run_tests.sh   (from tests/e2e/)

ZZ="${ZZ:-/home/zaid/Projects/zz_lang/target/debug/zz}"

"$ZZ" install >/dev/null

pass=0
fail=0
for f in src/*_test.zz; do
	name=$(basename "$f" .zz)
	# VM leg.
	out=$("$ZZ" run "$f" 2>&1 | grep -v openslide | grep -v VIPS-WARNING || true)
	if echo "$out" | grep -q "ALL_PASS"; then
		echo "PASS(vm)  $name"
		pass=$((pass + 1))
	else
		echo "FAIL(vm)  $name"
		echo "$out" | tail -n 8
		fail=$((fail + 1))
	fi
	# AOT leg (`zz build` emits src/bin/<stem>).
	if "$ZZ" build "$f" >/dev/null 2>&1 && [ -x "src/bin/$name" ]; then
		out=$(./src/bin/"$name" 2>&1 | grep -v openslide | grep -v VIPS-WARNING || true)
		if echo "$out" | grep -q "ALL_PASS"; then
			echo "PASS(aot) $name"
			pass=$((pass + 1))
		else
			echo "FAIL(aot) $name (no ALL_PASS)"
			echo "$out" | tail -n 8
			fail=$((fail + 1))
		fi
	else
		echo "FAIL(aot) $name (build failed)"
		fail=$((fail + 1))
	fi
done

echo ""
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
