#!/bin/bash

# Check for git and a git repo.
if head=`git rev-parse --verify HEAD 2>/dev/null`; then
	/bin/echo -n `git describe`

	# Are there uncommitted changes?
	git update-index --refresh --unmerged > /dev/null
	git diff-index --quiet HEAD || /bin/echo -n -dirty
else
# Check for version tag
	if [ -e version.tag ]; then
		/bin/echo -n `cat version.tag`
	fi
fi

echo
