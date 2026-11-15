#!/usr/bin/env bash
# Pushes this project to GitHub. Run through push.bat (Git Bash) or directly in Git Bash.
cd "$(dirname "$0")" || exit 1

REPO_URL="https://github.com/AmineTheJurk/conpile-anvilos-os.git"
BRANCH="main"

if [ ! -d .git ]; then
    echo "Initializing new git repository..."
    git init
    git branch -M "$BRANCH"
fi

if git remote get-url origin >/dev/null 2>&1; then
    git remote set-url origin "$REPO_URL"
else
    git remote add origin "$REPO_URL"
fi

echo
echo "Staging all changes..."
git add -A

echo
read -r -p "Enter commit name: " COMMIT_MSG
if [ -z "$COMMIT_MSG" ]; then
    echo "[ERROR] Commit name cannot be empty."
    read -r -p "Press Enter to close..."
    exit 1
fi

git commit -m "$COMMIT_MSG" || echo "Nothing to commit or commit failed."

echo
echo "Pushing to $REPO_URL ($BRANCH) - replacing everything in the repo..."
if git push -u --force origin "$BRANCH"; then
    echo
    echo "Done! Pushed to $REPO_URL"
else
    echo
    echo "[ERROR] Push failed. Check your GitHub sign-in and that the repo exists."
    read -r -p "Press Enter to close..."
    exit 1
fi
read -r -p "Press Enter to close..."
