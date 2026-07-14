#!/bin/bash

git -C external/AOWIS-SERVER-DB remote set-url origin \
    git@github.com:aowis-org/AOWIS-SERVER-DB.git

git config --local --get submodule.external/AOWIS-SERVER-DB.url
git -C external/AOWIS-SERVER-DB remote -v
