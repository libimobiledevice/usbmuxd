#!/bin/bash

# Check for git and a git repo.
if head=`git rev-parse --verify HEAD 2>/dev/null`; then
	echo -n `git describe`

	# Are there uncommitted changes?
	git update-index --refresh --unmerged > /dev/null
	git diff-index --quiet HEAD || echo -n -dirty
else
# Check for version tag
	if [ -e version.tag ]; then
		echo -n `cat version.tag`
	fi
fi

echo
