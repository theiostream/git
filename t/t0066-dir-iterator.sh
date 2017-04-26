#!/bin/sh

test_description='Test directory iteration.'

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir -p dir &&
	mkdir -p dir/a/b/c/ &&
	>dir/b &&
	>dir/c &&
	mkdir -p dir/d/e/d/ &&
	>dir/a/b/c/d &&
	>dir/a/e &&
	>dir/d/e/d/a &&

	mkdir -p dir2/a/b/c/ &&
	>dir2/a/b/c/d
'

test_expect_success 'dir-iterator should iterate through all files' '
	cat >expect-sorted-output <<-\EOF &&
	[d] (a) [a] ./dir/a
	[d] (a/b) [b] ./dir/a/b
	[d] (a/b/c) [c] ./dir/a/b/c
	[d] (d) [d] ./dir/d
	[d] (d/e) [e] ./dir/d/e
	[d] (d/e/d) [d] ./dir/d/e/d
	[f] (a/b/c/d) [d] ./dir/a/b/c/d
	[f] (a/e) [e] ./dir/a/e
	[f] (b) [b] ./dir/b
	[f] (c) [c] ./dir/c
	[f] (d/e/d/a) [a] ./dir/d/e/d/a
	EOF

	test-dir-iterator ./dir >out &&
	sort <out >./actual-pre-order-sorted-output &&

	test_cmp expect-sorted-output actual-pre-order-sorted-output
'

test_expect_success 'dir-iterator should list files in the correct order' '
	cat >expect-pre-order-output <<-\EOF &&
	[d] (a) [a] ./dir2/a
	[d] (a/b) [b] ./dir2/a/b
	[d] (a/b/c) [c] ./dir2/a/b/c
	[f] (a/b/c/d) [d] ./dir2/a/b/c/d
	EOF

	test-dir-iterator ./dir2 >actual-pre-order-output &&

	test_cmp expect-pre-order-output actual-pre-order-output
'

test_done
