---
name: git
description: >-
  Git workflow and branch modification policy for centipede-32z. Use whenever staging,
  committing, or manipulating git branches. Enforces that commits may only be made to
  branches starting with the prefix "work", while main and other branches are read-only.
---

# Git Skill: Branch Modification Policy

## Core Rule: Only Commit to "work*" Branches

- **You may commit to any branch whose name starts with the prefix `work`** (e.g., `work`, `work-sept25-try-coco3`, `work-feature-x`).
- **Committing to `main` or other branches is strictly for the user to do.**
- You can examine, inspect, read, diff, or log `main` and other branches whenever you need to, but **never commit to or modify them**.

---

## Pre-Commit Verification Procedure

Before running `git commit`, always verify the current branch:

```bash
git branch --show-current
```

1. **If the branch name starts with `work`** (e.g., `work-sept25-try-coco3`):
   - You may proceed with staging and committing files that belong strictly to the `centipede-32z` directory tree.

2. **If the branch name does NOT start with `work`** (e.g., `main`):
   - **DO NOT COMMIT.** Committing to `main` or other non-`work*` branches is reserved for the user.
   - Either switch to an existing `work*` branch or create a new branch prefixed with `work`:
     ```bash
     git checkout -b work-<descriptive-name>
     ```

---

## Read-Only Branch Rules

- You may freely inspect, view, diff, log, or checkout `main` and other branches for reference, research, and analysis.
- You must **NEVER** commit changes to, merge into, cherry-pick into, reset, rebase, or push to `main` or any branch not starting with `work`.
